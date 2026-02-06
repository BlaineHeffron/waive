#pragma once

#include <JuceHeader.h>

class UndoableCommandHandler;

//==============================================================================
/** In-process JSON command console for testing commands against the engine. */
class ConsoleComponent : public juce::Component
{
public:
    explicit ConsoleComponent (UndoableCommandHandler& handler);

    void resized() override;

private:
    void appendLog (const juce::String& text);

    UndoableCommandHandler& commandHandler;

    juce::Label statusLabel;
    juce::TextEditor requestEditor;
    juce::TextButton sendButton;
    juce::TextButton clearButton;
    juce::TextEditor responseEditor;
};
