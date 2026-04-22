#include "EditSession.h"

namespace
{
juce::ValueTree sanitiseStateForSnapshot (juce::ValueTree state)
{
    state.removeProperty (te::IDs::lastSignificantChange, nullptr);
    return state;
}

juce::String createStateSnapshot (juce::ValueTree state)
{
    if (auto xml = sanitiseStateForSnapshot (std::move (state)).createXml())
        return xml->toString();

    return {};
}
}

EditSession::EditSession (te::Engine& eng)
    : engine (eng)
{
    auto editFile = juce::File::createTempFile (".tracktionedit");
    edit = te::createEmptyEdit (engine, editFile);
    edit->getUndoManager().clearUndoHistory();
    lastSavedStateSnapshot = buildCurrentStateSnapshot();
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
    resetChangedStatus();
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
    {
        lastSavedStateSnapshot = buildCurrentStateSnapshot();
        edit->resetChangedStatus();
    }
}

void EditSession::markAsChanged()
{
    if (edit != nullptr)
    {
        edit->markAsChanged();
        listeners.call (&Listener::editStateChanged);
    }
}

void EditSession::setSavedStateFromFile (const juce::File& file)
{
    lastSavedStateSnapshot = buildStateSnapshotFromFile (file);
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

    auto& undoManager = edit->getUndoManager();
    const auto startedNewTransaction = ! coalesce || lastTransactionName != actionName;

    if (startedNewTransaction)
        undoManager.beginNewTransaction (actionName);

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
        if (startedNewTransaction && undoManager.getNumActionsInCurrentTransaction() > 0)
            undoManager.undoCurrentTransactionOnly();

        juce::Logger::writeToLog ("EditSession::performEdit failed: " + juce::String (e.what()));
    }
    catch (...)
    {
        if (startedNewTransaction && undoManager.getNumActionsInCurrentTransaction() > 0)
            undoManager.undoCurrentTransactionOnly();

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
    syncChangedStatusToSavedState();
    lastTransactionName.clear();
    listeners.call (&Listener::editStateChanged);
}

void EditSession::redo()
{
    edit->redo();
    syncChangedStatusToSavedState();
    lastTransactionName.clear();
    listeners.call (&Listener::editStateChanged);
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

juce::String EditSession::buildCurrentStateSnapshot() const
{
    if (edit == nullptr)
        return {};

    return createStateSnapshot (edit->state.createCopy());
}

juce::String EditSession::buildStateSnapshotFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return {};

    return createStateSnapshot (juce::ValueTree::fromXml (*xml));
}

void EditSession::syncChangedStatusToSavedState()
{
    if (edit == nullptr)
        return;

    if (buildCurrentStateSnapshot() == lastSavedStateSnapshot)
        edit->resetChangedStatus();
    else
        edit->markAsChanged();
}
