#pragma once

#include <JuceHeader.h>

class EditSession;
class AutoSaveManager;

//==============================================================================
/** Manages project file operations: new, open, save, save-as, recent files. */
class ProjectManager
{
public:
    /** Listener interface for project state changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void projectDirtyChanged() = 0;
        virtual void projectFileChanged (const juce::File&) {}
    };

    explicit ProjectManager (EditSession& session);
    ~ProjectManager();

    bool newProject();
    bool openProject();
    bool openProject (const juce::File& file);
    bool recoverProjectFromAutoSave (const juce::File& autoSaveFile,
                                     const juce::File& originalProjectFile);
    bool save();
    bool saveAs();

    bool isDirty() const;
    juce::File getCurrentFile() const   { return currentFile; }
    juce::String getProjectName() const;
    juce::StringArray getRecentFiles() const;
    void clearRecentFiles();

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    void notifyDirtyChanged();

private:
    void checkDirtyState();
    bool confirmSaveIfDirty();
    bool openProjectInternal (const juce::File& fileToLoad,
                              const juce::File& resultingProjectFile,
                              bool markChangedAfterLoad);
    void addToRecentFiles (const juce::File& file);

    EditSession& editSession;
    juce::File currentFile;

    mutable juce::ApplicationProperties appProperties;
    juce::ListenerList<Listener> listeners;
    bool lastDirtyState = false;
};
