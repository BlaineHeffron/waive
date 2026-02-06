#include "ConsoleComponent.h"
#include "UndoableCommandHandler.h"

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

    sendButton.setButtonText ("Send");
    clearButton.setButtonText ("Clear Log");

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
    auto bounds = getLocalBounds().reduced (12);

    statusLabel.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (8);

    auto requestArea = bounds.removeFromTop (160);
    requestEditor.setBounds (requestArea);
    bounds.removeFromTop (8);

    auto buttonRow = bounds.removeFromTop (32);
    sendButton.setBounds (buttonRow.removeFromLeft (100));
    buttonRow.removeFromLeft (8);
    clearButton.setBounds (buttonRow.removeFromLeft (100));

    bounds.removeFromTop (8);
    responseEditor.setBounds (bounds);
}

void ConsoleComponent::appendLog (const juce::String& text)
{
    responseEditor.moveCaretToEnd();
    responseEditor.insertTextAtCaret (text);
    responseEditor.moveCaretToEnd();
}
