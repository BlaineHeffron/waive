#include "ProjectManager.h"
#include "EditSession.h"
#include "AutoSaveManager.h"

#include <tracktion_engine/tracktion_engine.h>
#include <cstdlib>

namespace te = tracktion;

namespace
{

bool isHeadlessUiEnvironment()
{
#if JUCE_LINUX || JUCE_BSD
    const auto hasDisplayEnv = [] (const char* name)
    {
        if (const auto* value = std::getenv (name))
            return *value != '\0';

        return false;
    };

    return ! hasDisplayEnv ("DISPLAY") && ! hasDisplayEnv ("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

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
    if (! confirmSaveIfDirty())
        return false;

    editSession.createNew();
    currentFile = juce::File();
    listeners.call ([&] (Listener& listener) { listener.projectFileChanged (currentFile); });
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
        if (isHeadlessUiEnvironment())
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
            return openProjectInternal (autoSaveFile, file, true);

        discardAutoSaveOnSuccess = true;
    }

    if (! openProjectInternal (file, file, false))
        return false;

    if (discardAutoSaveOnSuccess)
        AutoSaveManager::deleteAutoSave (file);

    return true;
}

bool ProjectManager::openProjectInternal (const juce::File& fileToLoad,
                                         const juce::File& resultingProjectFile,
                                         bool markChangedAfterLoad)
{
    if (! confirmSaveIfDirty())
        return false;

    editSession.loadFromFile (fileToLoad);
    currentFile = resultingProjectFile;

    if (markChangedAfterLoad)
        editSession.markAsChanged();
    else
        editSession.resetChangedStatus();

    listeners.call ([&] (Listener& listener) { listener.projectFileChanged (currentFile); });
    addToRecentFiles (currentFile);
    checkDirtyState();
    return true;
}

bool ProjectManager::recoverProjectFromAutoSave (const juce::File& autoSaveFile,
                                                 const juce::File& originalProjectFile)
{
    if (! autoSaveFile.existsAsFile() || ! originalProjectFile.existsAsFile())
        return false;

    if (! confirmSaveIfDirty())
        return false;

    auto recoveryFile = juce::File::createTempFile (".tracktionedit");
    (void) recoveryFile.deleteFile();
    if (! autoSaveFile.copyFileTo (recoveryFile))
        return false;

    return openProjectInternal (recoveryFile, originalProjectFile, true);
}

bool ProjectManager::save()
{
    if (currentFile == juce::File())
        return saveAs();

    auto fileOps = te::EditFileOperations (editSession.getEdit());
    auto backingFile = fileOps.getEditFile();

    if (backingFile != juce::File() && backingFile != currentFile)
    {
        if (! fileOps.save (false, true, false))
            return false;

        if (! replaceFileAtomically (backingFile, currentFile))
            return false;
    }
    else
    {
        editSession.flushState();
        if (! fileOps.saveAs (currentFile))
            return false;
    }

    editSession.resetChangedStatus();
    AutoSaveManager::deleteAutoSave (currentFile);
    listeners.call ([&] (Listener& listener) { listener.projectFileChanged (currentFile); });
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

    auto fileOps = te::EditFileOperations (editSession.getEdit());
    auto backingFile = fileOps.getEditFile();

    if (backingFile != juce::File() && backingFile != file)
    {
        if (! fileOps.save (false, true, false))
            return false;

        if (! replaceFileAtomically (backingFile, file))
            return false;
    }
    else
    {
        editSession.flushState();
        if (! fileOps.saveAs (file))
            return false;
    }

    currentFile = file;
    editSession.resetChangedStatus();
    AutoSaveManager::deleteAutoSave (currentFile);
    listeners.call ([&] (Listener& listener) { listener.projectFileChanged (currentFile); });
    addToRecentFiles (file);
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
    juce::MessageManager::callAsync ([this]
    {
        checkDirtyState();
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

    if (isHeadlessUiEnvironment())
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
