#include "AutoSaveManager.h"
#include "EditSession.h"
#include "ProjectManager.h"

//==============================================================================
AutoSaveManager::AutoSaveManager (EditSession& session, ProjectManager& projectMgr, int intervalSeconds)
    : editSession (session), projectManager (projectMgr)
{
    startTimer (intervalSeconds * 1000);
}

AutoSaveManager::~AutoSaveManager()
{
    stopTimer();
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

    // Copy the edit file to .autosave
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
    auto currentFile = projectManager.getCurrentFile();
    if (currentFile == juce::File())
        return {};

    return currentFile.getSiblingFile (currentFile.getFileNameWithoutExtension() + ".autosave");
}

juce::File AutoSaveManager::checkForAutoSave (const juce::File& projectFile)
{
    if (projectFile == juce::File())
        return {};

    auto autoSave = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + ".autosave");
    return autoSave.existsAsFile() ? autoSave : juce::File();
}

void AutoSaveManager::deleteAutoSave (const juce::File& projectFile)
{
    if (projectFile == juce::File())
        return;

    auto autoSave = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + ".autosave");
    if (autoSave.existsAsFile())
        autoSave.deleteFile();
}
