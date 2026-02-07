#include "AiAgent.h"
#include "AiToolSchema.h"
#include "AiProvider.h"
#include "AiSettings.h"
#include "UndoableCommandHandler.h"
#include "ToolRegistry.h"
#include "JobQueue.h"

namespace waive
{

AiAgent::AiAgent (AiSettings& s, UndoableCommandHandler& handler,
                  ToolRegistry& registry, JobQueue& jq)
    : settings (s), commandHandler (handler), toolRegistry (registry), jobQueue (jq)
{
}

AiAgent::~AiAgent()
{
    cancelRequest();
}

void AiAgent::addListener (AiAgentListener* listener)
{
    listeners.add (listener);
}

void AiAgent::removeListener (AiAgentListener* listener)
{
    listeners.remove (listener);
}

std::vector<ChatMessage> AiAgent::getConversation() const
{
    std::lock_guard<std::mutex> lock (conversationMutex);
    return conversation;
}

void AiAgent::sendMessage (const juce::String& text)
{
    if (processing.load())
        return;

    {
        std::lock_guard<std::mutex> lock (conversationMutex);
        ChatMessage msg;
        msg.role = ChatMessage::Role::user;
        msg.content = text;
        conversation.push_back (std::move (msg));
    }

    juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });

    cancelRequested.store (false);
    processing.store (true);
    juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::processingStateChanged, true); });

    currentJobId = jobQueue.submit (
        { "AI Chat", "AI" },
        [this] (ProgressReporter&) { runConversationLoop(); },
        [this] (int, JobStatus)
        {
            processing.store (false);
            listeners.call (&AiAgentListener::processingStateChanged, false);
        });
}

void AiAgent::approvePendingToolCalls()
{
    pendingApproved.store (true);
    approvalEvent.signal();
}

void AiAgent::rejectPendingToolCalls()
{
    pendingApproved.store (false);
    approvalEvent.signal();
}

void AiAgent::clearConversation()
{
    cancelRequest();
    std::lock_guard<std::mutex> lock (conversationMutex);
    conversation.clear();
    juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });
}

void AiAgent::cancelRequest()
{
    cancelRequested.store (true);
    approvalEvent.signal();

    if (currentJobId >= 0)
        jobQueue.cancelJob (currentJobId);
}

void AiAgent::runConversationLoop()
{
    auto provider = createProvider (static_cast<int> (settings.getActiveProvider()));
    auto apiKey = settings.getApiKey (settings.getActiveProvider());
    auto model = settings.getSelectedModel (settings.getActiveProvider());
    auto systemPrompt = generateSystemPrompt();
    auto toolDefs = generateAllDefinitions (toolRegistry);

    const int maxIterations = 10;

    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        if (cancelRequested.load())
            return;

        // Get current conversation snapshot
        std::vector<ChatMessage> snapshot;
        {
            std::lock_guard<std::mutex> lock (conversationMutex);
            snapshot = conversation;
        }

        // Call the LLM
        auto response = provider->sendRequest (apiKey, model, systemPrompt, snapshot, toolDefs);

        if (cancelRequested.load())
            return;

        if (response.isError)
        {
            juce::MessageManager::callAsync ([this, err = response.errorMessage]
            {
                listeners.call (&AiAgentListener::aiErrorOccurred, err);
            });
            return;
        }

        // If no tool calls, add the text response and we're done
        if (response.toolCalls.empty())
        {
            {
                std::lock_guard<std::mutex> lock (conversationMutex);
                ChatMessage assistantMsg;
                assistantMsg.role = ChatMessage::Role::assistant;
                assistantMsg.content = response.textContent;
                conversation.push_back (std::move (assistantMsg));
            }
            juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });
            return;
        }

        // We have tool calls
        // Add assistant message with tool calls to conversation
        {
            std::lock_guard<std::mutex> lock (conversationMutex);
            ChatMessage assistantMsg;
            assistantMsg.role = ChatMessage::Role::assistant;
            assistantMsg.content = response.textContent;
            assistantMsg.toolCalls = response.toolCalls;
            conversation.push_back (std::move (assistantMsg));
        }
        juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });

        // Check auto-apply or ask for approval
        bool shouldExecute = settings.isAutoApply();

        if (! shouldExecute)
        {
            pendingToolCalls = response.toolCalls;
            pendingApproved.store (false);

            juce::MessageManager::callAsync ([this, calls = response.toolCalls]
            {
                listeners.call (&AiAgentListener::toolCallsPendingApproval, calls);
            });

            // Wait for user decision
            approvalEvent.wait (-1);
            approvalEvent.reset();

            if (cancelRequested.load())
                return;

            shouldExecute = pendingApproved.load();
            pendingToolCalls.clear();
        }

        // Execute or reject tool calls
        for (auto& tc : response.toolCalls)
        {
            if (cancelRequested.load())
                return;

            juce::String resultStr;

            if (shouldExecute)
            {
                resultStr = executeToolCall (tc);
            }
            else
            {
                resultStr = "{\"status\":\"rejected\",\"message\":\"User rejected this tool call.\"}";
            }

            {
                std::lock_guard<std::mutex> lock (conversationMutex);
                ChatMessage toolResultMsg;
                toolResultMsg.role = ChatMessage::Role::toolResult;
                toolResultMsg.toolCallId = tc.id;
                toolResultMsg.content = resultStr;
                conversation.push_back (std::move (toolResultMsg));
            }
            juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });
        }

        // Loop back to get the LLM's next response
    }
}

juce::String AiAgent::executeToolCall (const ChatMessage::ToolCall& call)
{
    if (call.name.startsWith ("cmd_"))
    {
        auto action = call.name.substring (4);  // strip "cmd_" prefix
        return executeCommand (action, call.arguments);
    }
    else if (call.name.startsWith ("tool_"))
    {
        // Higher-level tools not supported in the conversation loop for v1.
        // They need SessionComponent context, Plan/Apply flow, etc.
        return "{\"status\":\"error\",\"message\":\"High-level tool execution is not yet supported from AI chat. Use the Tool Sidebar instead.\"}";
    }

    return "{\"status\":\"error\",\"message\":\"Unknown tool: " + call.name + "\"}";
}

juce::String AiAgent::executeCommand (const juce::String& action, const juce::var& args)
{
    // Build the command JSON expected by CommandHandler
    auto* cmdObj = new juce::DynamicObject();
    cmdObj->setProperty ("action", action);

    // Copy arguments into the command object
    if (args.isObject())
    {
        if (auto* dynObj = args.getDynamicObject())
        {
            for (auto& prop : dynObj->getProperties())
                cmdObj->setProperty (prop.name, prop.value);
        }
    }

    auto cmdJson = juce::JSON::toString (juce::var (cmdObj));

    // Execute on the message thread for thread safety
    juce::String result;
    juce::WaitableEvent done;

    juce::MessageManager::callAsync ([this, cmdJson, &result, &done]
    {
        result = commandHandler.handleCommand (cmdJson);
        done.signal();
    });

    done.wait (10000);  // 10s timeout
    return result;
}

} // namespace waive
