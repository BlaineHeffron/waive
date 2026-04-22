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
    resetDirtyTrackingToCurrentState();
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
    lastTransactionWasCoalesced = false;
    undoTransactionGroupSizes.clear();
    redoTransactionGroupSizes.clear();
    undoTransactionDepth = 0;
    redoTransactionDepth = 0;
    resetDirtyTrackingToCurrentState();

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
        resetDirtyTrackingToCurrentState();
        edit->resetChangedStatus();
    }
}

void EditSession::markAsChanged()
{
    if (edit != nullptr)
    {
        hasNonUndoableUnsavedChange = true;
        edit->markAsChanged();
        listeners.call (&Listener::editStateChanged);
    }
}

void EditSession::setSavedStateFromFile (const juce::File& file)
{
    externalSavedStateSnapshot = buildStateSnapshotFromFile (file);
    savedUndoTransactionDepth = undoTransactionDepth;
    useExternalSavedStateSnapshot = true;
    hasNonUndoableUnsavedChange = false;
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
    const bool extendsCoalescedGroup = coalesce
                                    && lastTransactionWasCoalesced
                                    && lastTransactionName == actionName
                                    && ! undoTransactionGroupSizes.empty();

    undoManager.beginNewTransaction (actionName);

    const auto actionsBeforeMutation = undoManager.getNumActionsInCurrentTransaction();

    try
    {
        mutation (*edit);
        const auto actionsAfterMutation = undoManager.getNumActionsInCurrentTransaction();

        if (actionsAfterMutation > actionsBeforeMutation)
        {
            ++undoTransactionDepth;
            redoTransactionDepth = 0;
            redoTransactionGroupSizes.clear();

            if (extendsCoalescedGroup)
                ++undoTransactionGroupSizes.back();
            else
                undoTransactionGroupSizes.push_back (1);
        }
        else
        {
            hasNonUndoableUnsavedChange = true;
        }

        lastTransactionName = coalesce ? actionName : juce::String();
        lastTransactionWasCoalesced = coalesce;
        edit->markAsChanged();
        listeners.call (&Listener::editStateChanged);
        return true;
    }
    catch (const std::exception& e)
    {
        if (undoManager.getNumActionsInCurrentTransaction() > 0)
            undoManager.undoCurrentTransactionOnly();

        juce::Logger::writeToLog ("EditSession::performEdit failed: " + juce::String (e.what()));
    }
    catch (...)
    {
        if (undoManager.getNumActionsInCurrentTransaction() > 0)
            undoManager.undoCurrentTransactionOnly();

        juce::Logger::writeToLog ("EditSession::performEdit failed with unknown exception");
    }

    lastTransactionName.clear();
    lastTransactionWasCoalesced = false;
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
    const int transactionGroupSize = undoTransactionGroupSizes.empty() ? 1
                                                                       : undoTransactionGroupSizes.back();

    for (int i = 0; i < transactionGroupSize; ++i)
    {
        edit->undo();

        if (undoTransactionDepth > 0)
        {
            --undoTransactionDepth;
            ++redoTransactionDepth;
        }
    }

    if (! undoTransactionGroupSizes.empty())
    {
        redoTransactionGroupSizes.push_back (undoTransactionGroupSizes.back());
        undoTransactionGroupSizes.pop_back();
    }

    syncChangedStatusToSavedState();
    lastTransactionName.clear();
    lastTransactionWasCoalesced = false;
    listeners.call (&Listener::editStateChanged);
}

void EditSession::redo()
{
    const int transactionGroupSize = redoTransactionGroupSizes.empty() ? 1
                                                                       : redoTransactionGroupSizes.back();

    for (int i = 0; i < transactionGroupSize; ++i)
    {
        edit->redo();

        if (redoTransactionDepth > 0)
        {
            ++undoTransactionDepth;
            --redoTransactionDepth;
        }
    }

    if (! redoTransactionGroupSizes.empty())
    {
        undoTransactionGroupSizes.push_back (redoTransactionGroupSizes.back());
        redoTransactionGroupSizes.pop_back();
    }

    syncChangedStatusToSavedState();
    lastTransactionName.clear();
    lastTransactionWasCoalesced = false;
    listeners.call (&Listener::editStateChanged);
}

void EditSession::endCoalescedTransaction()
{
    lastTransactionName.clear();
    lastTransactionWasCoalesced = false;
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

void EditSession::resetDirtyTrackingToCurrentState()
{
    savedUndoTransactionDepth = undoTransactionDepth;
    hasNonUndoableUnsavedChange = false;
    useExternalSavedStateSnapshot = false;
    externalSavedStateSnapshot.clear();
}

void EditSession::syncChangedStatusToTrackingState()
{
    if (! hasNonUndoableUnsavedChange && undoTransactionDepth == savedUndoTransactionDepth)
        edit->resetChangedStatus();
    else
        edit->markAsChanged();
}

void EditSession::syncChangedStatusToSavedState()
{
    if (edit == nullptr)
        return;

    if (useExternalSavedStateSnapshot)
    {
        if (buildCurrentStateSnapshot() == externalSavedStateSnapshot)
        {
            hasNonUndoableUnsavedChange = false;
            edit->resetChangedStatus();
        }
        else
        {
            hasNonUndoableUnsavedChange = true;
            edit->markAsChanged();
        }
    }
    else
    {
        syncChangedStatusToTrackingState();
    }
}
