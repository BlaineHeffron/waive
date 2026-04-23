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
    statusLabel.setTooltip ("Shows the result of the most recent command");

    requestEditor.setMultiLine (true);
    requestEditor.setReturnKeyStartsNewLine (true);
    requestEditor.setText ("{ \"action\": \"ping\" }");
    requestEditor.setTitle ("Command Request");
    requestEditor.setDescription ("Enter a JSON command to send to the engine");
    requestEditor.setTooltip ("Enter JSON command here");
    requestEditor.setWantsKeyboardFocus (true);

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
    sendButton.setTooltip ("Send command");
    sendButton.setWantsKeyboardFocus (true);
    clearButton.setButtonText ("Clear Log");
    clearButton.setTitle ("Clear Response Log");
    clearButton.setDescription ("Clear the command response log");
    clearButton.setTooltip ("Clear response");
    clearButton.setWantsKeyboardFocus (true);

    addAndMakeVisible (statusLabel);
    addAndMakeVisible (requestEditor);
    addAndMakeVisible (sendButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (responseEditor);

    sendButton.onClick = [this]
    {
        submitRequest (requestEditor.getText());
    };

    clearButton.onClick = [this]
    {
        responseEditor.clear();
        updateStatus ("Console log cleared", false);
    };
}

void ConsoleComponent::submitRequestForTesting (const juce::String& request)
{
    submitRequest (request);
}

juce::String ConsoleComponent::getStatusTextForTesting() const
{
    return statusLabel.getText();
}

juce::String ConsoleComponent::getResponseLogTextForTesting() const
{
    return responseEditor.getText();
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
    if (! hasAppendedResponse)
    {
        responseEditor.clear();
        hasAppendedResponse = true;
    }

    responseEditor.moveCaretToEnd();
    responseEditor.insertTextAtCaret (text);
    responseEditor.moveCaretToEnd();
}

void ConsoleComponent::updateStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);

    if (auto* pal = waive::getWaivePalette (*this))
        statusLabel.setColour (juce::Label::textColourId, isError ? pal->danger : pal->textPrimary);
}

void ConsoleComponent::submitRequest (const juce::String& request)
{
    auto response = commandHandler.handleCommand (request);
    const auto parsed = juce::JSON::parse (response);
    const bool isError = parsed.isObject()
                         && (parsed["status"].toString() == "error"
                             || parsed.hasProperty ("error"));
    const auto prefix = isError ? "ERROR" : "OK";
    updateStatus (isError ? "Last command failed" : "Last command succeeded", isError);
    appendLog ("[" + juce::String (prefix) + "]\n> " + request + "\n" + response + "\n\n");
}
