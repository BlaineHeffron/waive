#include "UndoableCommandHandler.h"
#include "CommandHandler.h"
#include "EditSession.h"

const std::unordered_set<std::string> UndoableCommandHandler::readOnlyActions = {
    "ping",
    "get_tracks",
    "get_edit_state",
    "list_plugins",
    "transport_play",
    "transport_stop",
    "transport_seek"
};

UndoableCommandHandler::UndoableCommandHandler (CommandHandler& handler, EditSession& session)
    : commandHandler (&handler), editSession (session)
{
}

void UndoableCommandHandler::setCommandHandler (CommandHandler& handler)
{
    commandHandler = &handler;
}

juce::String UndoableCommandHandler::handleCommand (const juce::String& jsonString)
{
    return handleInternal (jsonString, false);
}

juce::String UndoableCommandHandler::handleCommandCoalesced (const juce::String& jsonString)
{
    return handleInternal (jsonString, true);
}

juce::String UndoableCommandHandler::handleInternal (const juce::String& jsonString, bool coalesce)
{
    auto parsed = juce::JSON::parse (jsonString);

    if (! parsed.isObject())
        return commandHandler->handleCommand (jsonString);

    auto action = parsed["action"].toString();

    // Read-only and transport commands pass through without undo wrapping.
    if (readOnlyActions.count (action.toStdString()) > 0)
        return commandHandler->handleCommand (jsonString);

    // Mutating command — wrap in undo transaction.
    juce::String result;
    auto ok = editSession.performEdit (action, coalesce, [&] (te::Edit&)
    {
        result = commandHandler->handleCommand (jsonString);
    });

    if (! ok)
        return "{ \"status\": \"error\", \"message\": \"Command failed during edit mutation\" }";

    return result;
}
