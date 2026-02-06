#pragma once

#include <JuceHeader.h>

class UndoableCommandHandler;

namespace waive
{

/** Send a JSON command object to the handler and return the response string. */
juce::String runCommand (UndoableCommandHandler& handler, const juce::var& commandObject);

/** Coalescing variant for slider drags. */
juce::String runCommandCoalesced (UndoableCommandHandler& handler, const juce::var& commandObject);

/** Create a minimal JSON command object with the given action name. */
juce::var makeAction (const juce::String& action);

} // namespace waive
