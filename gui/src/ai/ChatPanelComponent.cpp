#include "ChatPanelComponent.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

namespace waive
{

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
    addAndMakeVisible (messageDisplay);

    // Approval bar (hidden by default)
    addChildComponent (approveButton);
    addChildComponent (rejectButton);
    addChildComponent (approvalLabel);

    approvalLabel.setFont (waive::Fonts::caption());

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

    // Input bar
    inputField.setMultiLine (false);
    inputField.setTextToShowWhenEmpty ("Type a message...", juce::Colours::grey);
    inputField.setFont (waive::Fonts::body());
    inputField.onReturnKey = [this] { sendCurrentMessage(); };
    addAndMakeVisible (inputField);

    autoApplyToggle.setToggleState (settings.isAutoApply(), juce::dontSendNotification);
    autoApplyToggle.onClick = [this]
    {
        settings.setAutoApply (autoApplyToggle.getToggleState());
    };
    addAndMakeVisible (autoApplyToggle);

    sendButton.onClick = [this] { sendCurrentMessage(); };
    addAndMakeVisible (sendButton);

    clearButton.onClick = [this] { agent.clearConversation(); };
    addAndMakeVisible (clearButton);
}

ChatPanelComponent::~ChatPanelComponent()
{
    agent.removeListener (this);
}

void ChatPanelComponent::paint (juce::Graphics& g)
{
    if (auto* pal = getWaivePalette (*this))
        g.fillAll (pal->panelBg);
    else
        g.fillAll (juce::Colours::darkgrey);
}

void ChatPanelComponent::resized()
{
    auto bounds = getLocalBounds().reduced (Spacing::xs);

    // Header bar (28px)
    auto header = bounds.removeFromTop (28);
    settingsButton.setBounds (header.removeFromLeft (70));
    header.removeFromLeft (Spacing::xs);
    providerCombo.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (Spacing::xs);
    modelCombo.setBounds (header.removeFromLeft (180));
    header.removeFromLeft (Spacing::xs);
    statusLabel.setBounds (header);

    bounds.removeFromTop (Spacing::xs);

    // Input bar at bottom (28px)
    auto inputBar = bounds.removeFromBottom (28);
    autoApplyToggle.setBounds (inputBar.removeFromLeft (100));
    inputBar.removeFromLeft (Spacing::xs);
    clearButton.setBounds (inputBar.removeFromRight (50));
    inputBar.removeFromRight (Spacing::xs);
    sendButton.setBounds (inputBar.removeFromRight (50));
    inputBar.removeFromRight (Spacing::xs);
    inputField.setBounds (inputBar);

    bounds.removeFromBottom (Spacing::xs);

    // Approval bar (28px, conditional)
    if (showApprovalBar)
    {
        auto approvalBar = bounds.removeFromBottom (28);
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
        editor->onFocusLost = [this, editor, providerType]
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
    note->setColour (juce::Label::textColourId, juce::Colours::grey);
    panel->addAndMakeVisible (note);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle = "AI Settings";
    opts.dialogBackgroundColour = juce::Colours::darkgrey;
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

    auto selected = config.selectedModel;
    for (int i = 0; i < config.availableModels.size(); ++i)
    {
        if (config.availableModels[i] == selected)
        {
            modelCombo.setSelectedId (i + 1, juce::dontSendNotification);
            break;
        }
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
