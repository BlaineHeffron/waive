#include "ConsoleComponent.h"
#include "UndoableCommandHandler.h"
#include "WaiveSpacing.h"
#include "WaiveLookAndFeel.h"

//==============================================================================
ConsoleComponent::ConsoleComponent (UndoableCommandHandler& handler)
    : commandHandler (handler)
{
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText ("In-process command console", juce::dontSendNotification);

    requestEditor.setMultiLine (true);
    requestEditor.setReturnKeyStartsNewLine (true);
    requestEditor.setText ("{ \"action\": \"ping\" }");

    responseEditor.setMultiLine (true);
    responseEditor.setReadOnly (true);
    responseEditor.setReturnKeyStartsNewLine (true);
    responseEditor.setText ("Enter a JSON command above and click Send.\nSee docs/command_schema.json for available commands.");
    if (auto* pal = waive::getWaivePalette (*this))
        responseEditor.setColour (juce::TextEditor::textColourId, pal->textMuted);

    sendButton.setButtonText ("Send");
    sendButton.setTooltip ("Send command (Enter)");
    clearButton.setButtonText ("Clear Log");
    clearButton.setTooltip ("Clear response");

    requestEditor.setTooltip ("Enter JSON command here");

    addAndMakeVisible (statusLabel);
    addAndMakeVisible (requestEditor);
    addAndMakeVisible (sendButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (responseEditor);

    sendButton.onClick = [this]
    {
        auto request = requestEditor.getText();
        auto response = commandHandler.handleCommand (request);
        appendLog ("> " + request + "\n" + response + "\n\n");
    };

    clearButton.onClick = [this]
    {
        responseEditor.clear();
    };
}

void ConsoleComponent::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::md);

    statusLabel.setBounds (bounds.removeFromTop (waive::Spacing::xl));
    bounds.removeFromTop (waive::Spacing::sm);

    auto requestArea = bounds.removeFromTop (160);
    requestEditor.setBounds (requestArea);
    bounds.removeFromTop (waive::Spacing::sm);

    auto buttonRow = bounds.removeFromTop (waive::Spacing::controlHeightLarge);
    sendButton.setBounds (buttonRow.removeFromLeft (100));
    buttonRow.removeFromLeft (waive::Spacing::sm);
    clearButton.setBounds (buttonRow.removeFromLeft (100));

    bounds.removeFromTop (waive::Spacing::sm);
    responseEditor.setBounds (bounds);
}

void ConsoleComponent::appendLog (const juce::String& text)
{
    // Clear placeholder on first real response
    static bool firstAppend = true;
    if (firstAppend)
    {
        responseEditor.clear();
        if (auto* pal = waive::getWaivePalette (*this))
            responseEditor.setColour (juce::TextEditor::textColourId, pal->textPrimary);
        firstAppend = false;
    }

    // Check if response contains error key
    auto parsed = juce::JSON::parse (text);
    if (auto* pal = waive::getWaivePalette (*this))
    {
        if (parsed.isObject() && parsed.hasProperty ("error"))
            responseEditor.setColour (juce::TextEditor::textColourId, pal->danger);
        else
            responseEditor.setColour (juce::TextEditor::textColourId, pal->textPrimary);
    }

    responseEditor.moveCaretToEnd();
    responseEditor.insertTextAtCaret (text);
    responseEditor.moveCaretToEnd();
}
