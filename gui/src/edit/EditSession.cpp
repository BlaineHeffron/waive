#include "EditSession.h"

EditSession::EditSession (te::Engine& eng)
    : engine (eng)
{
    auto editFile = juce::File::createTempFile (".tracktionedit");
    edit = te::createEmptyEdit (engine, editFile);
    edit->getUndoManager().clearUndoHistory();
}

EditSession::~EditSession() = default;

te::Edit& EditSession::getEdit()
{
    return *edit;
}

//==============================================================================
void EditSession::replaceEdit (std::unique_ptr<te::Edit> newEdit)
{
    listeners.call (&Listener::editAboutToChange);

    edit = std::move (newEdit);
    lastTransactionName.clear();

    if (edit != nullptr)
        edit->getUndoManager().clearUndoHistory();

    listeners.call (&Listener::editChanged);
}

void EditSession::loadFromFile (const juce::File& file)
{
    auto newEdit = te::loadEditFromFile (engine, file);
    replaceEdit (std::move (newEdit));
}

void EditSession::createNew()
{
    auto editFile = juce::File::createTempFile (".tracktionedit");
    auto newEdit = te::createEmptyEdit (engine, editFile);
    newEdit->ensureNumberOfAudioTracks (1);
    replaceEdit (std::move (newEdit));
}

//==============================================================================
void EditSession::flushState()
{
    if (edit != nullptr)
        edit->flushState();
}

bool EditSession::hasChangedSinceSaved() const
{
    return edit != nullptr && edit->hasChangedSinceSaved();
}

void EditSession::resetChangedStatus()
{
    if (edit != nullptr)
        edit->resetChangedStatus();
}

//==============================================================================
bool EditSession::performEdit (const juce::String& actionName,
                               std::function<void (te::Edit&)> mutation)
{
    return performEdit (actionName, false, std::move (mutation));
}

bool EditSession::performEdit (const juce::String& actionName,
                               bool coalesce,
                               std::function<void (te::Edit&)> mutation)
{
    if (edit == nullptr)
        return false;

    if (! coalesce || lastTransactionName != actionName)
        edit->getUndoManager().beginNewTransaction (actionName);

    lastTransactionName = actionName;

    try
    {
        mutation (*edit);
        edit->markAsChanged();
        listeners.call (&Listener::editStateChanged);
        return true;
    }
    catch (const std::exception& e)
    {
        edit->getUndoManager().undo();
        juce::Logger::writeToLog ("EditSession::performEdit failed: " + juce::String (e.what()));
    }
    catch (...)
    {
        edit->getUndoManager().undo();
        juce::Logger::writeToLog ("EditSession::performEdit failed with unknown exception");
    }

    lastTransactionName.clear();
    return false;
}

bool EditSession::canUndo() const
{
    return edit->getUndoManager().canUndo();
}

bool EditSession::canRedo() const
{
    return edit->getUndoManager().canRedo();
}

void EditSession::undo()
{
    edit->undo();
    lastTransactionName.clear();
}

void EditSession::redo()
{
    edit->redo();
    lastTransactionName.clear();
}

void EditSession::endCoalescedTransaction()
{
    lastTransactionName.clear();
}

juce::String EditSession::getUndoDescription() const
{
    return edit->getUndoManager().getUndoDescription();
}

juce::String EditSession::getRedoDescription() const
{
    return edit->getUndoManager().getRedoDescription();
}
