#include "UndoableCommandHandler.h"
#include "CommandHandler.h"
#include "EditSession.h"
#include "ProjectManager.h"

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

bool isPassThroughAction (const juce::String& action)
{
    return isReadOnlyAction (action)
        || action == "export_mixdown"
        || action == "export_stems"
        || action == "package_as_zip"
        || action == "collect_and_save"
        || action == "remove_unused_media";
}

bool marksProjectAsSaved (const juce::String& action, const juce::var& response)
{
    if (! response.isObject() || response["status"].toString() != "ok")
        return false;

    return action == "collect_and_save";
}

void deleteAutoSaveForProjectFile (const juce::File& projectFile)
{
    if (projectFile == juce::File())
        return;

    auto autoSave = projectFile.getSiblingFile (".waive-autosave-"
                                                + projectFile.getFileNameWithoutExtension()
                                                + ".tracktionedit");
    if (autoSave.existsAsFile())
        (void) autoSave.deleteFile();
}
}

UndoableCommandHandler::UndoableCommandHandler (CommandHandler& handler, EditSession& session,
                                                ProjectManager* owningProjectManager)
    : commandHandler (&handler), editSession (session), projectManager (owningProjectManager)
{
}

void UndoableCommandHandler::setCommandHandler (CommandHandler& handler)
{
    commandHandler = &handler;
}

void UndoableCommandHandler::setAllowedMediaDirectories (const juce::Array<juce::File>& directories)
{
    if (commandHandler != nullptr)
        commandHandler->setAllowedMediaDirectories (directories);
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

    // Read-only queries and file-side-effect commands pass through without undo wrapping.
    if (isPassThroughAction (action))
    {
        auto result = commandHandler->handleCommand (jsonString);
        auto response = juce::JSON::parse (result);

        if (marksProjectAsSaved (action, response))
        {
            editSession.resetChangedStatus();

            if (projectManager != nullptr)
                deleteAutoSaveForProjectFile (projectManager->getCurrentFile());
        }

        return result;
    }

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
