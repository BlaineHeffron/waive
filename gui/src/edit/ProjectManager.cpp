#include "ProjectManager.h"
#include "EditSession.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

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

ProjectManager::~ProjectManager() = default;

//==============================================================================
bool ProjectManager::newProject()
{
    if (! confirmSaveIfDirty())
        return false;

    editSession.createNew();
    currentFile = juce::File();
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

    if (! confirmSaveIfDirty())
        return false;

    editSession.loadFromFile (file);
    currentFile = file;
    editSession.resetChangedStatus();
    addToRecentFiles (file);
    return true;
}

bool ProjectManager::save()
{
    if (currentFile == juce::File())
        return saveAs();

    editSession.flushState();
    te::EditFileOperations (editSession.getEdit()).saveAs (currentFile);
    editSession.resetChangedStatus();
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

    editSession.flushState();
    te::EditFileOperations (editSession.getEdit()).saveAs (file);
    currentFile = file;
    editSession.resetChangedStatus();
    addToRecentFiles (file);
    return true;
}

//==============================================================================
bool ProjectManager::isDirty() const
{
    return editSession.hasChangedSinceSaved();
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
