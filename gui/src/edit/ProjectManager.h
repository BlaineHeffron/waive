#pragma once

#include <JuceHeader.h>
#include "EditSession.h"


//==============================================================================
/** Manages project file operations: new, open, save, save-as, recent files. */
class ProjectManager
{
public:
    enum class FileChangeKind
    {
        newProject,
        openProject,
        recoverProject,
        save,
        saveAs
    };

    /** Listener interface for project state changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void projectDirtyChanged() = 0;
        virtual void projectFileChanged (const juce::File& previousProjectFile,
                                         const juce::File& currentProjectFile,
                                         FileChangeKind changeKind)
        {
            juce::ignoreUnused (previousProjectFile, currentProjectFile, changeKind);
        }
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
    bool saveAs (const juce::File& file);
    bool confirmSaveIfDirty();
    void markCurrentProjectSaved();

    bool isDirty() const
    {
        return editSession.hasChangedSinceSaved();
    }
    juce::File getCurrentFile() const   { return currentFile; }
    juce::String getProjectName() const;
    juce::StringArray getRecentFiles() const;
    void clearRecentFiles();

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    void notifyDirtyChanged()
    {
        if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating())
        {
            if (messageManager->isThisTheMessageThread())
            {
                checkDirtyState();
                return;
            }
        }

        juce::WeakReference<ProjectManager> weakThis (this);
        juce::MessageManager::callAsync ([weakThis]
        {
            if (weakThis != nullptr)
                weakThis->checkDirtyState();
        });
    }

private:
    void checkDirtyState()
    {
        bool currentDirty = isDirty();
        if (currentDirty != lastDirtyState)
        {
            lastDirtyState = currentDirty;
            listeners.call (&Listener::projectDirtyChanged);
        }
    }
    void discardUnsavedChanges();
    bool prepareForProjectTransition (bool discardCurrentAutoSaveOnDiscard,
                                      const juce::File& preservedAutoSaveFile = juce::File());
    bool openProjectInternal (const juce::File& fileToLoad,
                              const juce::File& resultingProjectFile,
                              bool markChangedAfterLoad,
                              bool discardCurrentAutoSaveOnDiscard = false,
                              bool prepareCurrentProject = true);
    void addToRecentFiles (const juce::File& file);

    EditSession& editSession;
    juce::File currentFile;

    mutable juce::ApplicationProperties appProperties;
    juce::ListenerList<Listener> listeners;
    bool lastDirtyState = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ProjectManager)
};
