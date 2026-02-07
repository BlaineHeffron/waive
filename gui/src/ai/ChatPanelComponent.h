#pragma once

#include <JuceHeader.h>
#include "AiAgent.h"
#include "AiSettings.h"

namespace waive
{

//==============================================================================
class ChatPanelComponent : public juce::Component,
                           public AiAgentListener
{
public:
    ChatPanelComponent (AiAgent& agent, AiSettings& settings);
    ~ChatPanelComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // AiAgentListener
    void conversationUpdated() override;
    void toolCallsPendingApproval (const std::vector<ChatMessage::ToolCall>& calls) override;
    void processingStateChanged (bool isProcessing) override;
    void aiErrorOccurred (const juce::String& error) override;

private:
    void sendCurrentMessage();
    void refreshMessageDisplay();
    void showSettingsDialog();
    void updateProviderCombo();
    void updateModelCombo();

    AiAgent& agent;
    AiSettings& settings;

    // Header bar
    juce::TextButton settingsButton { "Settings" };
    juce::ComboBox providerCombo;
    juce::ComboBox modelCombo;
    juce::Label statusLabel;

    // Message display
    juce::TextEditor messageDisplay;

    // Approval bar
    juce::TextButton approveButton { "Approve" };
    juce::TextButton rejectButton { "Reject" };
    juce::Label approvalLabel;
    bool showApprovalBar = false;

    // Input bar
    juce::ToggleButton autoApplyToggle { "Auto-apply" };
    juce::TextEditor inputField;
    juce::TextButton sendButton { "Send" };
    juce::TextButton clearButton { "Clear" };
};

} // namespace waive
