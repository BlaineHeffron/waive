#pragma once

#include <JuceHeader.h>

class EditSession;
class ProjectManager;

//==============================================================================
/** Periodically auto-saves edits and manages recovery files. */
class AutoSaveManager : public juce::Timer
{
public:
    AutoSaveManager (EditSession& session, ProjectManager& projectMgr, int intervalSeconds = 300);
    ~AutoSaveManager() override;

    /** Check if an auto-save file exists for the given project. */
    static juce::File checkForAutoSave (const juce::File& projectFile);

    /** Delete the auto-save file for the given project. */
    static void deleteAutoSave (const juce::File& projectFile);

    /** Returns the configured auto-save interval in seconds. */
    static int getConfiguredIntervalSeconds();

    /** Returns the auto-save file path for a project file. */
    static juce::File getAutoSaveFileForProject (const juce::File& projectFile);

    /** Deterministic test hook for writing the current auto-save snapshot immediately. */
    void triggerAutoSaveForTesting();

private:
    void timerCallback() override;
    juce::File getAutoSaveFile() const;
    void writeAutoSaveSnapshot();

    EditSession& editSession;
    ProjectManager& projectManager;
    juce::ApplicationProperties appProperties;
};
