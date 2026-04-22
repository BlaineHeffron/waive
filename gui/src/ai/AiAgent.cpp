#include "AiAgent.h"
#include "AiToolSchema.h"
#include "AiProvider.h"
#include "AiSettings.h"
#include "ChatHistorySerializer.h"
#include "UndoableCommandHandler.h"
#include "EditSession.h"
#include "ToolRegistry.h"
#include "JobQueue.h"
#include "Tool.h"

namespace waive
{

namespace
{
bool containsToolDefinition (const std::vector<AiToolDefinition>& defs, const juce::String& name)
{
    for (const auto& def : defs)
        if (def.name == name)
            return true;

    return false;
}

void appendUniqueToolDefinitions (std::vector<AiToolDefinition>& dst,
                                  const std::vector<AiToolDefinition>& src)
{
    for (const auto& def : src)
        if (! containsToolDefinition (dst, def.name))
            dst.push_back (def);
}
} // namespace

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

void AiAgent::setToolContextProvider (ToolContextProvider provider)
{
    toolContextProvider = std::move (provider);
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

    notifyConversationUpdatedAsync();

    cancelRequested.store (false);
    processing.store (true);
    notifyProcessingStateChangedAsync (true);

    juce::WeakReference<AiAgent> safeThis (this);

    currentJobId = jobQueue.submit (
        { "AI Chat", "AI" },
        [safeThis] (ProgressReporter&)
        {
            if (safeThis != nullptr)
                safeThis->runConversationLoop();
        },
        [safeThis] (int, JobStatus)
        {
            if (safeThis == nullptr)
                return;

            safeThis->processing.store (false);
            safeThis->notifyProcessingStateChangedAsync (false);
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
    {
        std::lock_guard<std::mutex> lock (conversationMutex);
        conversation.clear();
    }
    discoveredTools.clear();
    notifyConversationUpdatedAsync();
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

    discoveredTools.clear();

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

        // Update tool definitions with discovered tools
        auto currentToolDefs = generateCoreDefinitions();
        appendUniqueToolDefinitions (currentToolDefs, discoveredTools);

        // Call the LLM
        auto response = provider->sendRequest (apiKey, model, systemPrompt, snapshot, currentToolDefs);

        if (cancelRequested.load())
            return;

        if (response.isError)
        {
            notifyAiErrorAsync (response.errorMessage);
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
            notifyConversationUpdatedAsync();
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
        notifyConversationUpdatedAsync();

        // Check auto-apply or ask for approval
        bool shouldExecute = settings.isAutoApply();

        if (! shouldExecute)
        {
            pendingToolCalls = response.toolCalls;
            pendingApproved.store (false);

            notifyToolCallsPendingApprovalAsync (response.toolCalls);

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
                toolResultMsg.toolName = tc.name;
                toolResultMsg.content = resultStr;
                conversation.push_back (std::move (toolResultMsg));
            }
            notifyConversationUpdatedAsync();
        }

        // Loop back to get the LLM's next response
    }
}

juce::String AiAgent::executeToolCall (const ChatMessage::ToolCall& call)
{
    // Handle undo/redo specially — these need EditSession, not CommandHandler
    if (call.name == "cmd_undo")
    {
        auto& editSession = commandHandler.getEditSession();
        if (editSession.getUndoDescription().isNotEmpty())
        {
            auto desc = editSession.getUndoDescription();
            editSession.undo();
            return "{\"status\":\"ok\",\"undone\":\"" + desc.replace ("\"", "\\\"") + "\"}";
        }
        return "{\"status\":\"ok\",\"message\":\"Nothing to undo\"}";
    }

    if (call.name == "cmd_redo")
    {
        auto& editSession = commandHandler.getEditSession();
        if (editSession.getRedoDescription().isNotEmpty())
        {
            auto desc = editSession.getRedoDescription();
            editSession.redo();
            return "{\"status\":\"ok\",\"redone\":\"" + desc.replace ("\"", "\\\"") + "\"}";
        }
        return "{\"status\":\"ok\",\"message\":\"Nothing to redo\"}";
    }

    // Handle search_tools specially — it returns schemas and adds them to the active set
    if (call.name == "cmd_search_tools")
    {
        auto query = call.arguments.hasProperty ("query")
                       ? call.arguments["query"].toString()
                       : juce::String();

        if (query.isEmpty())
            return "{\"status\":\"error\",\"message\":\"Missing query parameter\"}";

        auto results = searchDefinitions (toolRegistry, query);
        auto coreDefs = generateCoreDefinitions();

        // Add discovered tools to the active set (avoid duplicates)
        for (auto& r : results)
        {
            if (! containsToolDefinition (coreDefs, r.name)
                && ! containsToolDefinition (discoveredTools, r.name))
                discoveredTools.push_back (r);
        }

        // Build response
        auto* resultObj = new juce::DynamicObject();
        resultObj->setProperty ("status", "ok");
        resultObj->setProperty ("count", (int) results.size());

        juce::Array<juce::var> toolList;
        for (auto& r : results)
        {
            auto* tObj = new juce::DynamicObject();
            tObj->setProperty ("name", r.name);
            tObj->setProperty ("description", r.description);
            tObj->setProperty ("category", r.category);
            // Include input schema so the AI knows the parameters
            tObj->setProperty ("input_schema", r.inputSchema);
            toolList.add (juce::var (tObj));
        }
        resultObj->setProperty ("tools", toolList);

        if (results.empty())
            resultObj->setProperty ("message", "No tools found matching: " + query);
        else
            resultObj->setProperty ("message", juce::String (results.size()) + " tools found. You can now call them directly.");

        return juce::JSON::toString (juce::var (resultObj));
    }

    if (call.name.startsWith ("cmd_"))
    {
        auto action = call.name.substring (4);  // strip "cmd_" prefix
        return executeCommand (action, call.arguments);
    }
    else if (call.name.startsWith ("tool_"))
    {
        auto toolName = call.name.substring (5);  // strip "tool_" prefix
        return executeTool (toolName, call.arguments);
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

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return commandHandler.handleCommand (cmdJson);

    juce::MessageManagerLock messageManagerLock;
    if (! messageManagerLock.lockWasGained())
        return "{\"status\":\"error\",\"message\":\"Failed to acquire message-thread lock.\"}";

    return commandHandler.handleCommand (cmdJson);
}

void AiAgent::saveConversation (const juce::File& file)
{
    std::lock_guard<std::mutex> lock (conversationMutex);
    ChatHistorySerializer::saveChatHistory (conversation, file);
}

void AiAgent::loadConversation (const juce::File& file)
{
    auto loaded = ChatHistorySerializer::loadChatHistory (file);

    if (loaded.empty())
    {
        // Distinguish between "file not found" vs "parse error"
        if (file.existsAsFile())
        {
            DBG ("AiAgent::loadConversation - Failed to parse chat history file: " + file.getFullPathName());
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock (conversationMutex);
        conversation = std::move (loaded);
    }

    notifyConversationUpdatedAsync();
}

juce::String AiAgent::executeTool (const juce::String& toolName, const juce::var& args)
{
    if (! toolContextProvider)
        return "{\"status\":\"error\",\"message\":\"Tool execution context not configured.\"}";

    // Find the tool
    auto* tool = toolRegistry.findTool (toolName);
    if (tool == nullptr)
        return "{\"status\":\"error\",\"message\":\"Unknown tool: " + toolName + "\"}";

    // Step 1: preparePlan on message thread (needs UI/edit access)
    ToolPlanTask task;
    juce::Result prepareResult = juce::Result::fail ("Not executed");

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto context = toolContextProvider();
        prepareResult = tool->preparePlan (context, args, task);
    }
    else
    {
        juce::MessageManagerLock messageManagerLock;
        if (! messageManagerLock.lockWasGained())
            return "{\"status\":\"error\",\"message\":\"Failed to acquire message-thread lock for tool planning.\"}";

        auto context = toolContextProvider();
        prepareResult = tool->preparePlan (context, args, task);
    }

    if (prepareResult.failed())
        return "{\"status\":\"error\",\"message\":\"Plan preparation failed: "
               + prepareResult.getErrorMessage().replace ("\"", "\\\"") + "\"}";

    // Step 2: run task on current thread (already background)
    // Create a simple reporter
    class SimpleReporter : public ProgressReporter
    {
    public:
        SimpleReporter()
            : ProgressReporter (0, cancelFlag, [](int, float, const juce::String&) {})
        {
        }

    private:
        std::atomic<bool> cancelFlag { false };
    };

    SimpleReporter reporter;
    auto plan = task.run (reporter);

    // Step 3: apply on message thread
    juce::Result applyResult = juce::Result::fail ("Not executed");

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto context = toolContextProvider();
        applyResult = tool->apply (context, plan);
    }
    else
    {
        juce::MessageManagerLock messageManagerLock;
        if (! messageManagerLock.lockWasGained())
            return "{\"status\":\"error\",\"message\":\"Failed to acquire message-thread lock for tool apply.\"}";

        auto context = toolContextProvider();
        applyResult = tool->apply (context, plan);
    }

    if (applyResult.failed())
        return "{\"status\":\"error\",\"message\":\"Apply failed: "
               + applyResult.getErrorMessage().replace ("\"", "\\\"") + "\"}";

    // Build success response
    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("summary", plan.summary);
    result->setProperty ("changes", plan.changes.size());
    return juce::JSON::toString (juce::var (result));
}

void AiAgent::notifyConversationUpdatedAsync()
{
    juce::WeakReference<AiAgent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->listeners.call (&AiAgentListener::conversationUpdated);
    });
}

void AiAgent::notifyProcessingStateChangedAsync (bool isProcessingNow)
{
    juce::WeakReference<AiAgent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, isProcessingNow]
    {
        if (safeThis != nullptr)
            safeThis->listeners.call (&AiAgentListener::processingStateChanged, isProcessingNow);
    });
}

void AiAgent::notifyAiErrorAsync (const juce::String& error)
{
    juce::WeakReference<AiAgent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, error]
    {
        if (safeThis != nullptr)
            safeThis->listeners.call (&AiAgentListener::aiErrorOccurred, error);
    });
}

void AiAgent::notifyToolCallsPendingApprovalAsync (const std::vector<ChatMessage::ToolCall>& calls)
{
    juce::WeakReference<AiAgent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, calls]
    {
        if (safeThis != nullptr)
            safeThis->listeners.call (&AiAgentListener::toolCallsPendingApproval, calls);
    });
}

} // namespace waive
