#pragma once

#include <JuceHeader.h>

class EditSession;

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
    };

    explicit ProjectManager (EditSession& session);
    ~ProjectManager();

    bool newProject();
    bool openProject();
    bool openProject (const juce::File& file);
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
    void addToRecentFiles (const juce::File& file);

    EditSession& editSession;
    juce::File currentFile;

    mutable juce::ApplicationProperties appProperties;
    juce::ListenerList<Listener> listeners;
    bool lastDirtyState = false;
};
