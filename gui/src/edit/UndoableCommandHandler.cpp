#include "UndoableCommandHandler.h"
#include "CommandHandler.h"
#include "EditSession.h"

#include <stdexcept>

namespace
{
struct CommandFailedException : public std::runtime_error
{
    explicit CommandFailedException (juce::String failureMessage)
        : std::runtime_error (failureMessage.toStdString()),
          message (std::move (failureMessage))
    {
    }

    juce::String message;
};

bool isReadOnlyAction (const juce::String& action)
{
    if (action == "ping")
        return true;

    if (action == "transport_play" || action == "transport_stop" || action == "transport_seek")
        return true;

    return action.startsWith ("get_") || action.startsWith ("list_");
}
}

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
    if (isReadOnlyAction (action))
        return commandHandler->handleCommand (jsonString);

    // Mutating command — wrap in undo transaction.
    juce::String result;
    juce::String failureMessage;
    auto ok = editSession.performEdit (action, coalesce, [&] (te::Edit&)
    {
        result = commandHandler->handleCommand (jsonString);

        auto response = juce::JSON::parse (result);
        if (response.isObject() && response["status"].toString() == "error")
        {
            failureMessage = response["message"].toString();
            throw CommandFailedException (failureMessage);
        }
    });

    if (! ok)
    {
        if (failureMessage.isNotEmpty())
            return result;

        return "{ \"status\": \"error\", \"message\": \"Command failed during edit mutation\" }";
    }

    return result;
}
