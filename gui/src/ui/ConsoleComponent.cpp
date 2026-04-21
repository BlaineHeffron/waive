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
    statusLabel.setTitle ("Console Status");
    statusLabel.setDescription ("Status for the in-process JSON command console");

    requestEditor.setMultiLine (true);
    requestEditor.setReturnKeyStartsNewLine (true);
    requestEditor.setText ("{ \"action\": \"ping\" }");
    requestEditor.setTitle ("Command Request");
    requestEditor.setDescription ("Enter a JSON command to send to the engine");
    requestEditor.setTooltip ("Enter JSON command here");

    responseEditor.setMultiLine (true);
    responseEditor.setReadOnly (true);
    responseEditor.setReturnKeyStartsNewLine (true);
    responseEditor.setText ("Enter a JSON command above and click Send.\nSee docs/command_schema.json for available commands.");
    responseEditor.setTitle ("Command Response Log");
    responseEditor.setDescription ("Shows command responses and errors");
    responseEditor.setTooltip ("Command responses and errors");
    if (auto* pal = waive::getWaivePalette (*this))
        responseEditor.setColour (juce::TextEditor::textColourId, pal->textMuted);

    sendButton.setButtonText ("Send");
    sendButton.setTitle ("Send Command");
    sendButton.setDescription ("Send the JSON command to the engine");
    sendButton.setTooltip ("Send command (Enter)");
    clearButton.setButtonText ("Clear Log");
    clearButton.setTitle ("Clear Response Log");
    clearButton.setDescription ("Clear the command response log");
    clearButton.setTooltip ("Clear response");

    addAndMakeVisible (statusLabel);
    addAndMakeVisible (requestEditor);
    addAndMakeVisible (sendButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (responseEditor);

    sendButton.onClick = [this]
    {
        auto request = requestEditor.getText();
        auto response = commandHandler.handleCommand (request);
        const auto parsed = juce::JSON::parse (response);
        const bool isError = parsed.isObject() && parsed.hasProperty ("error");
        appendLog ("> " + request + "\n" + response + "\n\n", isError);
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

void ConsoleComponent::appendLog (const juce::String& text, bool isError)
{
    // Clear placeholder on first real response
    if (! hasAppendedResponse)
    {
        responseEditor.clear();
        if (auto* pal = waive::getWaivePalette (*this))
            responseEditor.setColour (juce::TextEditor::textColourId, pal->textPrimary);
        hasAppendedResponse = true;
    }

    if (auto* pal = waive::getWaivePalette (*this))
    {
        if (isError)
            responseEditor.setColour (juce::TextEditor::textColourId, pal->danger);
        else
            responseEditor.setColour (juce::TextEditor::textColourId, pal->textPrimary);
    }

    responseEditor.moveCaretToEnd();
    responseEditor.insertTextAtCaret (text);
    responseEditor.moveCaretToEnd();
}
