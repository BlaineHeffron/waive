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

    if (deleteAutoSaveOnShutdown)
        deleteAutoSave (projectManager.getCurrentFile());
}

void AutoSaveManager::markCleanShutdown()
{
    deleteAutoSaveOnShutdown = true;
}

void AutoSaveManager::timerCallback()
{
    writeAutoSaveSnapshot();
}

void AutoSaveManager::triggerAutoSaveForTesting()
{
    writeAutoSaveSnapshot();
}

void AutoSaveManager::writeAutoSaveSnapshot()
{
    if (! editSession.hasChangedSinceSaved())
        return;

    auto currentFile = projectManager.getCurrentFile();
    if (currentFile == juce::File())
        return;

    auto projectDir = currentFile.getParentDirectory();
    if (projectDir == juce::File() || ! projectDir.exists())
        return;

    auto autoSaveFile = getAutoSaveFile();
    if (autoSaveFile == juce::File())
        return;

    // Persist a snapshot of the live edit state, not just the last explicit save on disk.
    editSession.flushState();

    auto xml = editSession.getEdit().state.createXml();
    if (xml == nullptr || ! xml->writeTo (autoSaveFile, {}))
    {
        juce::Logger::writeToLog ("AutoSaveManager: Failed to save auto-save snapshot to "
                                  + autoSaveFile.getFullPathName());
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
