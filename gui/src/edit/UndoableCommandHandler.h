#pragma once

#include <JuceHeader.h>
#include <unordered_set>

class CommandHandler;
class EditSession;

//==============================================================================
/** Wraps CommandHandler, adding undo transactions for mutating commands.
    Read-only commands pass through directly; mutating commands are wrapped
    in EditSession::performEdit so they participate in undo/redo. */
class UndoableCommandHandler
{
public:
    UndoableCommandHandler (CommandHandler& handler, EditSession& session);

    /** Reseat the underlying CommandHandler (used after edit swap). */
    void setCommandHandler (CommandHandler& handler);

    /** Process a JSON command string, wrapping mutations in undo transactions. */
    juce::String handleCommand (const juce::String& jsonString);

    /** Coalescing variant for slider drags — skips beginNewTransaction when the
        previous transaction had the same action name. */
    juce::String handleCommandCoalesced (const juce::String& jsonString);

private:
    juce::String handleInternal (const juce::String& jsonString, bool coalesce);

    CommandHandler* commandHandler;
    EditSession& editSession;

    static const std::unordered_set<std::string> readOnlyActions;
};
