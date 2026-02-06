#include "CommandHelpers.h"
#include "UndoableCommandHandler.h"

namespace waive
{

juce::String runCommand (UndoableCommandHandler& handler, const juce::var& commandObject)
{
    return handler.handleCommand (juce::JSON::toString (commandObject));
}

juce::String runCommandCoalesced (UndoableCommandHandler& handler, const juce::var& commandObject)
{
    return handler.handleCommandCoalesced (juce::JSON::toString (commandObject));
}

juce::var makeAction (const juce::String& action)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("action", action);
    return juce::var (obj);
}

} // namespace waive
