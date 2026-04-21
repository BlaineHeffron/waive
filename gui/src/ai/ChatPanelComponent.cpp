#include "ChatPanelComponent.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

namespace waive
{

namespace
{
juce::Colour getMutedTextColour (juce::Component& component)
{
    if (auto* pal = getWaivePalette (component))
        return pal->textMuted;

    return juce::Colour (0xff808080);
}

juce::Colour getPanelBackgroundColour (juce::Component& component)
{
    if (auto* pal = getWaivePalette (component))
        return pal->panelBg;

    return juce::Colour (0xff2b2b2b);
}
}

ChatPanelComponent::ChatPanelComponent (AiAgent& a, AiSettings& s)
    : agent (a), settings (s)
{
    agent.addListener (this);

    // Header
    addAndMakeVisible (settingsButton);
    addAndMakeVisible (providerCombo);
    addAndMakeVisible (modelCombo);
    addAndMakeVisible (statusLabel);

    statusLabel.setText ("Ready", juce::dontSendNotification);
    statusLabel.setFont (waive::Fonts::caption());
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setTitle ("AI Status");
    statusLabel.setDescription ("Current AI provider and request status");
    statusLabel.setTooltip ("Current AI status");

    settingsButton.setTitle ("AI Settings");
    settingsButton.setDescription ("Open AI provider and credential settings");
    settingsButton.setTooltip ("Open AI settings");
    settingsButton.setWantsKeyboardFocus (true);

    providerCombo.setTitle ("AI Provider");
    providerCombo.setDescription ("Select the active AI provider");
    providerCombo.setTooltip ("Select AI provider");
    providerCombo.setWantsKeyboardFocus (true);

    modelCombo.setTitle ("AI Model");
    modelCombo.setDescription ("Select the active AI model");
    modelCombo.setTooltip ("Select AI model");
    modelCombo.setWantsKeyboardFocus (true);

    settingsButton.onClick = [this] { showSettingsDialog(); };

    updateProviderCombo();
    updateModelCombo();

    providerCombo.onChange = [this]
    {
        auto idx = providerCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) settings.getAllProviders().size())
        {
            settings.setActiveProvider (settings.getAllProviders()[(size_t) idx].type);
            updateModelCombo();
        }
    };

    modelCombo.onChange = [this]
    {
        auto text = modelCombo.getText();
        if (text.isNotEmpty())
            settings.setSelectedModel (settings.getActiveProvider(), text);
    };

    // Message display
    messageDisplay.setMultiLine (true);
    messageDisplay.setReadOnly (true);
    messageDisplay.setScrollbarsShown (true);
    messageDisplay.setFont (waive::Fonts::body());
    messageDisplay.setTitle ("Conversation");
    messageDisplay.setDescription ("Conversation history with the AI assistant");
    messageDisplay.setTooltip ("Conversation history");
    addAndMakeVisible (messageDisplay);

    // Approval bar (hidden by default)
    addChildComponent (approveButton);
    addChildComponent (rejectButton);
    addChildComponent (approvalLabel);

    approvalLabel.setFont (waive::Fonts::caption());
    approvalLabel.setTitle ("Pending Tool Approval");
    approvalLabel.setDescription ("Shows when AI tool calls need approval");
    approvalLabel.setTooltip ("Pending tool approval");

    approveButton.setTitle ("Approve");
    approveButton.setDescription ("Approve the pending AI tool calls");
    approveButton.setTooltip ("Approve pending AI tool calls");
    approveButton.setWantsKeyboardFocus (true);

    approveButton.onClick = [this]
    {
        agent.approvePendingToolCalls();
        showApprovalBar = false;
        resized();
    };

    rejectButton.onClick = [this]
    {
        agent.rejectPendingToolCalls();
        showApprovalBar = false;
        resized();
    };
    rejectButton.setTitle ("Reject");
    rejectButton.setDescription ("Reject the pending AI tool calls");
    rejectButton.setTooltip ("Reject pending AI tool calls");
    rejectButton.setWantsKeyboardFocus (true);

    // Input bar
    inputField.setMultiLine (false);
    inputField.setTextToShowWhenEmpty ("Type a message...", getMutedTextColour (*this));
    inputField.setFont (waive::Fonts::body());
    inputField.setTitle ("Message Input");
    inputField.setDescription ("Type a message to send to the AI assistant");
    inputField.setTooltip ("Type a message");
    inputField.onReturnKey = [this] { sendCurrentMessage(); };
    addAndMakeVisible (inputField);

    autoApplyToggle.setToggleState (settings.isAutoApply(), juce::dontSendNotification);
    autoApplyToggle.setTitle ("Auto Apply");
    autoApplyToggle.setDescription ("Automatically apply approved AI tool changes");
    autoApplyToggle.setTooltip ("Toggle auto-apply for AI tool changes");
    autoApplyToggle.setWantsKeyboardFocus (true);
    autoApplyToggle.onClick = [this]
    {
        settings.setAutoApply (autoApplyToggle.getToggleState());
    };
    addAndMakeVisible (autoApplyToggle);

    sendButton.setTitle ("Send");
    sendButton.setDescription ("Send the current message to the AI assistant");
    sendButton.setTooltip ("Send message");
    sendButton.setWantsKeyboardFocus (true);
    sendButton.onClick = [this] { sendCurrentMessage(); };
    addAndMakeVisible (sendButton);

    clearButton.setTitle ("Clear");
    clearButton.setDescription ("Clear the current AI conversation");
    clearButton.setTooltip ("Clear conversation");
    clearButton.setWantsKeyboardFocus (true);
    clearButton.onClick = [this] { agent.clearConversation(); };
    addAndMakeVisible (clearButton);
}

ChatPanelComponent::~ChatPanelComponent()
{
    agent.removeListener (this);
}

void ChatPanelComponent::paint (juce::Graphics& g)
{
    auto* pal = getWaivePalette (*this);
    g.fillAll (getPanelBackgroundColour (*this));

    // Empty state message when no messages
    if (agent.getConversation().empty())
    {
        g.setFont (waive::Fonts::caption());
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        auto messageBounds = getLocalBounds().reduced (Spacing::xs)
                                             .removeFromTop (getHeight() - Spacing::controlHeightDefault * 2 - Spacing::xs * 4);
        g.drawText ("No messages yet", messageBounds, juce::Justification::centred, true);
    }
}

void ChatPanelComponent::resized()
{
    auto bounds = getLocalBounds().reduced (Spacing::xs);

    // Header bar
    auto header = bounds.removeFromTop (Spacing::controlHeightDefault);
    settingsButton.setBounds (header.removeFromLeft (70));
    header.removeFromLeft (Spacing::xs);
    providerCombo.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (Spacing::xs);
    modelCombo.setBounds (header.removeFromLeft (180));
    header.removeFromLeft (Spacing::xs);
    statusLabel.setBounds (header);

    bounds.removeFromTop (Spacing::xs);

    // Input bar at bottom (use controlHeightLarge for adequate multi-line input space)
    auto inputBar = bounds.removeFromBottom (Spacing::controlHeightLarge);
    autoApplyToggle.setBounds (inputBar.removeFromLeft (100));
    inputBar.removeFromLeft (Spacing::xs);
    clearButton.setBounds (inputBar.removeFromRight (50).removeFromTop (Spacing::controlHeightDefault));
    inputBar.removeFromRight (Spacing::xs);
    sendButton.setBounds (inputBar.removeFromRight (50).removeFromTop (Spacing::controlHeightDefault));
    inputBar.removeFromRight (Spacing::xs);
    inputField.setBounds (inputBar);

    bounds.removeFromBottom (Spacing::xs);

    // Approval bar (conditional)
    if (showApprovalBar)
    {
        auto approvalBar = bounds.removeFromBottom (Spacing::controlHeightDefault);
        approveButton.setBounds (approvalBar.removeFromLeft (70));
        approvalBar.removeFromLeft (Spacing::xs);
        rejectButton.setBounds (approvalBar.removeFromLeft (60));
        approvalBar.removeFromLeft (Spacing::xs);
        approvalLabel.setBounds (approvalBar);

        approveButton.setVisible (true);
        rejectButton.setVisible (true);
        approvalLabel.setVisible (true);

        bounds.removeFromBottom (Spacing::xs);
    }
    else
    {
        approveButton.setVisible (false);
        rejectButton.setVisible (false);
        approvalLabel.setVisible (false);
    }

    // Message display fills the rest
    messageDisplay.setBounds (bounds);
}

void ChatPanelComponent::sendCurrentMessage()
{
    auto text = inputField.getText().trim();
    if (text.isEmpty())
        return;

    inputField.clear();
    agent.sendMessage (text);
}

void ChatPanelComponent::refreshMessageDisplay()
{
    auto msgs = agent.getConversation();
    juce::String displayText;

    for (auto& msg : msgs)
    {
        switch (msg.role)
        {
            case ChatMessage::Role::user:
                displayText += "You: " + msg.content + "\n\n";
                break;
            case ChatMessage::Role::assistant:
                displayText += "Waive AI: " + msg.content + "\n";
                for (auto& tc : msg.toolCalls)
                    displayText += "  [tool] " + tc.name + "\n";
                displayText += "\n";
                break;
            case ChatMessage::Role::toolResult:
            {
                auto result = juce::JSON::parse (msg.content);
                auto status = result["status"].toString();
                displayText += "  -> " + status;
                if (result.hasProperty ("message"))
                    displayText += ": " + result["message"].toString();
                displayText += "\n\n";
                break;
            }
            case ChatMessage::Role::system:
                break;
        }
    }

    messageDisplay.setText (displayText, juce::dontSendNotification);
    messageDisplay.moveCaretToEnd();
}

void ChatPanelComponent::showSettingsDialog()
{
    auto* panel = new juce::Component();
    panel->setSize (400, 300);

    int y = 10;
    auto& providers = settings.getAllProviders();

    for (size_t i = 0; i < providers.size(); ++i)
    {
        auto& p = providers[i];

        auto* label = new juce::Label();
        label->setText (p.displayName + " API Key:", juce::dontSendNotification);
        label->setBounds (10, y, 150, 24);
        panel->addAndMakeVisible (label);

        auto* editor = new juce::TextEditor();
        editor->setPasswordCharacter ('*');
        editor->setText (p.apiKey, juce::dontSendNotification);
        editor->setBounds (170, y, 220, 24);

        auto providerType = p.type;
        editor->onTextChange = [this, editor, providerType]
        {
            settings.setApiKey (providerType, editor->getText());
        };

        panel->addAndMakeVisible (editor);
        y += 34;
    }

    // Add a note about persistence
    auto* note = new juce::Label();
    note->setText ("Keys are saved in application settings.", juce::dontSendNotification);
    note->setFont (waive::Fonts::caption());
    note->setBounds (10, y + 10, 380, 20);
    note->setColour (juce::Label::textColourId, getMutedTextColour (*this));
    panel->addAndMakeVisible (note);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle = "AI Settings";
    opts.dialogBackgroundColour = getPanelBackgroundColour (*this);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.runModal();
}

void ChatPanelComponent::updateProviderCombo()
{
    providerCombo.clear (juce::dontSendNotification);
    auto& providers = settings.getAllProviders();
    for (size_t i = 0; i < providers.size(); ++i)
        providerCombo.addItem (providers[i].displayName, (int) i + 1);

    auto active = settings.getActiveProvider();
    for (size_t i = 0; i < providers.size(); ++i)
    {
        if (providers[i].type == active)
        {
            providerCombo.setSelectedId ((int) i + 1, juce::dontSendNotification);
            break;
        }
    }
}

void ChatPanelComponent::updateModelCombo()
{
    modelCombo.clear (juce::dontSendNotification);
    auto& config = settings.getProviderConfig (settings.getActiveProvider());

    for (int i = 0; i < config.availableModels.size(); ++i)
        modelCombo.addItem (config.availableModels[i], i + 1);

    bool selectedInList = false;
    auto selected = config.selectedModel;
    for (int i = 0; i < config.availableModels.size(); ++i)
    {
        if (config.availableModels[i] == selected)
        {
            modelCombo.setSelectedId (i + 1, juce::dontSendNotification);
            selectedInList = true;
            break;
        }
    }

    if (! selectedInList && config.availableModels.size() > 0)
    {
        modelCombo.setSelectedId (1, juce::dontSendNotification);
        settings.setSelectedModel (settings.getActiveProvider(), config.availableModels[0]);
    }
}

// AiAgentListener callbacks
void ChatPanelComponent::conversationUpdated()
{
    refreshMessageDisplay();
}

void ChatPanelComponent::toolCallsPendingApproval (const std::vector<ChatMessage::ToolCall>& calls)
{
    juce::String desc;
    for (auto& tc : calls)
    {
        if (desc.isNotEmpty())
            desc += ", ";
        desc += tc.name;
    }

    approvalLabel.setText ("AI wants to: " + desc, juce::dontSendNotification);
    showApprovalBar = true;
    resized();
}

void ChatPanelComponent::processingStateChanged (bool isProcessing)
{
    statusLabel.setText (isProcessing ? "Thinking..." : "Ready", juce::dontSendNotification);
    sendButton.setEnabled (! isProcessing);
    inputField.setEnabled (! isProcessing);
}

void ChatPanelComponent::aiErrorOccurred (const juce::String& error)
{
    statusLabel.setText ("Error", juce::dontSendNotification);

    juce::String display = messageDisplay.getText();
    display += "[Error] " + error + "\n\n";
    messageDisplay.setText (display, juce::dontSendNotification);
    messageDisplay.moveCaretToEnd();
}

} // namespace waive
