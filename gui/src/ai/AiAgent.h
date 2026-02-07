#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <vector>
#include <atomic>

#include "AiProvider.h"
#include "AiSettings.h"
#include "Tool.h"

class UndoableCommandHandler;

namespace waive
{

class ToolRegistry;
class JobQueue;

//==============================================================================
class AiAgentListener
{
public:
    virtual ~AiAgentListener() = default;
    virtual void conversationUpdated() = 0;
    virtual void toolCallsPendingApproval (const std::vector<ChatMessage::ToolCall>& calls) = 0;
    virtual void processingStateChanged (bool isProcessing) = 0;
    virtual void aiErrorOccurred (const juce::String& error) = 0;
};

//==============================================================================
class AiAgent
{
public:
    AiAgent (AiSettings& settings, UndoableCommandHandler& handler,
             ToolRegistry& registry, JobQueue& jobQueue);
    ~AiAgent();

    void sendMessage (const juce::String& text);
    void approvePendingToolCalls();
    void rejectPendingToolCalls();
    void clearConversation();
    void cancelRequest();

    /** Save the current conversation to a JSON file. */
    void saveConversation (const juce::File& file);

    /** Load a conversation from a JSON file, replacing current. */
    void loadConversation (const juce::File& file);

    std::vector<ChatMessage> getConversation() const;
    bool isProcessing() const { return processing.load(); }

    void addListener (AiAgentListener* listener);
    void removeListener (AiAgentListener* listener);

    using ToolContextProvider = std::function<waive::ToolExecutionContext()>;

    /** Set a callback that provides the execution context for tool_* calls. */
    void setToolContextProvider (ToolContextProvider provider);

private:
    void runConversationLoop();
    juce::String executeToolCall (const ChatMessage::ToolCall& call);
    juce::String executeCommand (const juce::String& action, const juce::var& args);
    juce::String executeTool (const juce::String& toolName, const juce::var& args);

    AiSettings& settings;
    UndoableCommandHandler& commandHandler;
    ToolRegistry& toolRegistry;
    JobQueue& jobQueue;

    std::vector<ChatMessage> conversation;
    mutable std::mutex conversationMutex;

    std::vector<AiToolDefinition> discoveredTools;
    std::vector<ChatMessage::ToolCall> pendingToolCalls;
    juce::WaitableEvent approvalEvent;
    std::atomic<bool> pendingApproved { false };
    std::atomic<bool> processing { false };
    std::atomic<bool> cancelRequested { false };
    int currentJobId = -1;

    ToolContextProvider toolContextProvider;

    juce::ListenerList<AiAgentListener> listeners;
};

} // namespace waive
