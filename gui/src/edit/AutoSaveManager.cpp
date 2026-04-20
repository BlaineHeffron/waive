#include "AutoSaveManager.h"
#include "EditSession.h"
#include "ProjectManager.h"

//==============================================================================
AutoSaveManager::AutoSaveManager (EditSession& session, ProjectManager& projectMgr, int intervalSeconds)
    : editSession (session), projectManager (projectMgr)
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Waive";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support/Waive";
    appProperties.setStorageParameters (opts);
    startTimer (juce::jmax (1, intervalSeconds) * 1000);
}

AutoSaveManager::~AutoSaveManager()
{
    stopTimer();
    deleteAutoSave (projectManager.getCurrentFile());
}

void AutoSaveManager::timerCallback()
{
    if (! editSession.hasChangedSinceSaved())
        return;

    auto autoSaveFile = getAutoSaveFile();
    if (autoSaveFile == juce::File())
        return;

    // Save current edit state
    editSession.flushState();

    // Copy the saved project file to the current recovery snapshot path.
    auto currentFile = projectManager.getCurrentFile();
    if (currentFile.existsAsFile())
    {
        if (! currentFile.copyFileTo (autoSaveFile))
        {
            juce::Logger::writeToLog ("AutoSaveManager: Failed to copy file to " + autoSaveFile.getFullPathName());
        }
    }
}

juce::File AutoSaveManager::getAutoSaveFile() const
{
    return getAutoSaveFileForProject (projectManager.getCurrentFile());
}

juce::File AutoSaveManager::checkForAutoSave (const juce::File& projectFile)
{
    auto autoSave = getAutoSaveFileForProject (projectFile);
    if (! autoSave.existsAsFile())
        return {};

    if (! projectFile.existsAsFile())
        return autoSave;

    return autoSave.getLastModificationTime() > projectFile.getLastModificationTime()
             ? autoSave
             : juce::File();
}

void AutoSaveManager::deleteAutoSave (const juce::File& projectFile)
{
    auto autoSave = getAutoSaveFileForProject (projectFile);
    if (autoSave.existsAsFile())
        autoSave.deleteFile();
}

int AutoSaveManager::getConfiguredIntervalSeconds()
{
    juce::ApplicationProperties properties;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Waive";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support/Waive";
    properties.setStorageParameters (opts);

    constexpr int defaultIntervalSeconds = 300;
    if (auto* settings = properties.getUserSettings())
        return juce::jlimit (30, 3600, settings->getIntValue ("autoSaveIntervalSeconds", defaultIntervalSeconds));

    return defaultIntervalSeconds;
}

juce::File AutoSaveManager::getAutoSaveFileForProject (const juce::File& projectFile)
{
    if (projectFile == juce::File())
        return {};

    return projectFile.getSiblingFile (".waive-autosave-"
                                       + projectFile.getFileNameWithoutExtension()
                                       + ".tracktionedit");
}
