#include "ProjectManager.h"
#include "EditSession.h"
#include "AutoSaveManager.h"
#include "UiMessageHelpers.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace
{
bool replaceFileAtomically (const juce::File& sourceFile, const juce::File& destinationFile)
{
    if (! sourceFile.existsAsFile())
        return false;

    auto tempFile = destinationFile.getSiblingFile (destinationFile.getFileName()
                                                    + ".tmp-"
                                                    + juce::Uuid().toString());

    if (tempFile.exists())
        (void) tempFile.deleteFile();

    if (! sourceFile.copyFileTo (tempFile))
        return false;

    const bool success = destinationFile.existsAsFile()
                           ? tempFile.replaceFileIn (destinationFile)
                           : tempFile.moveFileTo (destinationFile);

    if (! success && tempFile.exists())
        (void) tempFile.deleteFile();

    return success;
}

void deleteTransientBackingFileIfNeeded (const juce::File& backingFile,
                                         const juce::File& destinationFile)
{
    if (backingFile == juce::File() || backingFile == destinationFile)
        return;

    const auto tempDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory);
    if (! backingFile.isAChildOf (tempDirectory))
        return;

    if (backingFile.existsAsFile())
        (void) backingFile.deleteFile();
}

void rebindEditBackingFile (te::Edit& edit, const juce::File& targetFile)
{
    edit.editFileRetriever = [targetFile] { return targetFile; };
}

}

//==============================================================================
ProjectManager::ProjectManager (EditSession& session)
    : editSession (session)
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Waive";
    opts.filenameSuffix       = ".settings";
    opts.osxLibrarySubFolder = "Application Support/Waive";
    appProperties.setStorageParameters (opts);
}

ProjectManager::~ProjectManager()
{
}

//==============================================================================
bool ProjectManager::newProject()
{
    const bool hadUnsavedChanges = isDirty();
    const auto previousFile = currentFile;
    if (! confirmSaveIfDirty())
        return false;

    if (hadUnsavedChanges && isDirty())
        discardUnsavedChanges();

    editSession.createNew();
    currentFile = juce::File();
    listeners.call ([&] (Listener& listener)
                    { listener.projectFileChanged (previousFile, currentFile, FileChangeKind::newProject); });
    checkDirtyState();
    return true;
}

bool ProjectManager::openProject()
{
    juce::FileChooser chooser ("Open Project", juce::File(), "*.tracktionedit");

    if (! chooser.browseForFileToOpen())
        return false;

    return openProject (chooser.getResult());
}

bool ProjectManager::openProject (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    bool discardAutoSaveOnSuccess = false;
    if (const auto autoSaveFile = AutoSaveManager::checkForAutoSave (file);
        autoSaveFile != juce::File())
    {
        bool shouldRecover = false;
        if (! waive::canShowUiDialogs())
        {
            juce::Logger::writeToLog ("ProjectManager: headless environment detected, auto-recovering newer autosave");
            shouldRecover = true;
        }
        else
        {
            shouldRecover = juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Recover Unsaved Changes?",
                "A newer auto-save was found for this project. Recover it before opening?",
                "Recover", "Open Saved Version");
        }

        if (shouldRecover)
        {
            if (! prepareForProjectTransition (true, autoSaveFile))
                return false;

            return openProjectInternal (autoSaveFile, file, true, false, false);
        }

        discardAutoSaveOnSuccess = true;
    }

    if (! openProjectInternal (file, file, false, true))
        return false;

    if (discardAutoSaveOnSuccess)
        AutoSaveManager::deleteAutoSave (file);

    return true;
}

bool ProjectManager::prepareForProjectTransition (bool discardCurrentAutoSaveOnDiscard,
                                                  const juce::File& preservedAutoSaveFile)
{
    const bool hadUnsavedChanges = isDirty();
    if (! confirmSaveIfDirty())
        return false;

    if (discardCurrentAutoSaveOnDiscard && hadUnsavedChanges && isDirty())
    {
        const auto currentAutoSaveFile = currentFile != juce::File()
                                           ? AutoSaveManager::getAutoSaveFileForProject (currentFile)
                                           : juce::File();
        if (currentAutoSaveFile != preservedAutoSaveFile)
            discardUnsavedChanges();
    }

    return true;
}

bool ProjectManager::openProjectInternal (const juce::File& fileToLoad,
                                         const juce::File& resultingProjectFile,
                                         bool markChangedAfterLoad,
                                         bool discardCurrentAutoSaveOnDiscard,
                                         bool prepareCurrentProject)
{
    const auto previousFile = currentFile;
    if (prepareCurrentProject && ! prepareForProjectTransition (discardCurrentAutoSaveOnDiscard))
        return false;

    editSession.loadFromFile (fileToLoad);
    currentFile = resultingProjectFile;

    if (markChangedAfterLoad)
    {
        editSession.setSavedStateFromFile (resultingProjectFile);
        editSession.markAsChanged();
    }
    else
        editSession.resetChangedStatus();

    const auto changeKind = markChangedAfterLoad ? FileChangeKind::recoverProject
                                                 : FileChangeKind::openProject;
    listeners.call ([&] (Listener& listener)
                    { listener.projectFileChanged (previousFile, currentFile, changeKind); });
    addToRecentFiles (currentFile);
    checkDirtyState();
    return true;
}

bool ProjectManager::recoverProjectFromAutoSave (const juce::File& autoSaveFile,
                                                 const juce::File& originalProjectFile)
{
    if (! autoSaveFile.existsAsFile() || ! originalProjectFile.existsAsFile())
        return false;

    if (! prepareForProjectTransition (true, autoSaveFile))
        return false;

    auto recoveryFile = juce::File::createTempFile (".tracktionedit");
    (void) recoveryFile.deleteFile();
    if (! autoSaveFile.copyFileTo (recoveryFile))
        return false;

    return openProjectInternal (recoveryFile, originalProjectFile, true, false, false);
}

bool ProjectManager::save()
{
    if (currentFile == juce::File())
        return saveAs();

    const auto previousFile = currentFile;
    auto fileOps = te::EditFileOperations (editSession.getEdit());
    auto backingFile = fileOps.getEditFile();

    if (backingFile != juce::File() && backingFile != currentFile)
    {
        if (! fileOps.save (false, true, false))
            return false;

        if (! replaceFileAtomically (backingFile, currentFile))
            return false;

        if (te::EditFileOperations (editSession.getEdit()).getEditFile() != currentFile)
            rebindEditBackingFile (editSession.getEdit(), currentFile);
    }
    else
    {
        editSession.flushState();
        if (! fileOps.saveAs (currentFile))
            return false;
    }

    deleteTransientBackingFileIfNeeded (backingFile, currentFile);
    editSession.resetChangedStatus();
    AutoSaveManager::deleteAutoSave (currentFile);
    listeners.call ([&] (Listener& listener)
                    { listener.projectFileChanged (previousFile, currentFile, FileChangeKind::save); });
    checkDirtyState();
    return true;
}

bool ProjectManager::saveAs()
{
    juce::FileChooser chooser ("Save Project As", currentFile, "*.tracktionedit");

    if (! chooser.browseForFileToSave (true))
        return false;

    auto file = chooser.getResult();
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension ("tracktionedit");

    return saveAs (file);
}

bool ProjectManager::saveAs (const juce::File& file)
{
    auto targetFile = file;
    if (targetFile == juce::File())
        return false;

    if (targetFile.getFileExtension().isEmpty())
        targetFile = targetFile.withFileExtension ("tracktionedit");

    const auto parentDirectory = targetFile.getParentDirectory();
    if (parentDirectory == juce::File() || ! parentDirectory.exists())
        return false;

    auto fileOps = te::EditFileOperations (editSession.getEdit());
    auto backingFile = fileOps.getEditFile();
    const auto previousFile = currentFile;
    editSession.flushState();
    if (! fileOps.saveAs (targetFile, true))
        return false;

    // Tracktion's saveAs can leave the live edit bound to its previous backing file.
    // Rebind the current Edit in place so undo history and edit-scoped UI state survive Save As.
    if (te::EditFileOperations (editSession.getEdit()).getEditFile() != targetFile)
        rebindEditBackingFile (editSession.getEdit(), targetFile);

    deleteTransientBackingFileIfNeeded (backingFile, targetFile);

    if (previousFile != juce::File() && previousFile != targetFile)
        AutoSaveManager::deleteAutoSave (previousFile);

    currentFile = targetFile;
    editSession.resetChangedStatus();
    AutoSaveManager::deleteAutoSave (currentFile);
    listeners.call ([&] (Listener& listener)
                    { listener.projectFileChanged (previousFile, currentFile, FileChangeKind::saveAs); });
    addToRecentFiles (targetFile);
    checkDirtyState();
    return true;
}

//==============================================================================
bool ProjectManager::isDirty() const
{
    return editSession.hasChangedSinceSaved();
}

void ProjectManager::addListener (Listener* listener)
{
    listeners.add (listener);
}

void ProjectManager::removeListener (Listener* listener)
{
    listeners.remove (listener);
}

void ProjectManager::notifyDirtyChanged()
{
    juce::WeakReference<ProjectManager> weakThis (this);
    juce::MessageManager::callAsync ([weakThis]
    {
        if (weakThis != nullptr)
            weakThis->checkDirtyState();
    });
}

void ProjectManager::checkDirtyState()
{
    bool currentDirty = isDirty();
    if (currentDirty != lastDirtyState)
    {
        lastDirtyState = currentDirty;
        listeners.call (&Listener::projectDirtyChanged);
    }
}

void ProjectManager::discardUnsavedChanges()
{
    AutoSaveManager::deleteAutoSave (currentFile);
}

juce::String ProjectManager::getProjectName() const
{
    if (currentFile != juce::File())
        return currentFile.getFileNameWithoutExtension();

    return "Untitled";
}

juce::StringArray ProjectManager::getRecentFiles() const
{
    juce::StringArray files;
    if (auto* props = appProperties.getUserSettings())
        files.addTokens (props->getValue ("recentFiles"), "|", "");

    files.removeEmptyStrings();
    return files;
}

void ProjectManager::clearRecentFiles()
{
    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue ("recentFiles", "");
        props->saveIfNeeded();
    }
}

//==============================================================================
bool ProjectManager::confirmSaveIfDirty()
{
    if (! isDirty())
        return true;

    if (! waive::canShowUiDialogs())
    {
        juce::Logger::writeToLog ("ProjectManager: headless environment detected, discarding unsaved changes without prompt");
        return true;
    }

    auto result = juce::AlertWindow::showYesNoCancelBox (
        juce::AlertWindow::QuestionIcon,
        "Unsaved Changes",
        "Do you want to save changes to " + getProjectName() + "?",
        "Save", "Don't Save", "Cancel", nullptr, nullptr);

    if (result == 0)  // Cancel
        return false;

    if (result == 1)  // Save
        return save();

    return true;  // Don't Save
}

void ProjectManager::addToRecentFiles (const juce::File& file)
{
    auto files = getRecentFiles();

    files.removeString (file.getFullPathName());
    files.insert (0, file.getFullPathName());

    while (files.size() > 10)
        files.remove (files.size() - 1);

    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue ("recentFiles", files.joinIntoString ("|"));
        props->saveIfNeeded();
    }
}
