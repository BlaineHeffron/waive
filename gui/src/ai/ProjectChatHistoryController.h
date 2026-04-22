#pragma once

#include <JuceHeader.h>

#include "../edit/ProjectManager.h"

namespace waive
{

class AiAgent;

class ProjectChatHistoryController : private ProjectManager::Listener
{
public:
    ProjectChatHistoryController (AiAgent& agent, ProjectManager& projectManager);
    ~ProjectChatHistoryController() override;

    void loadCurrentConversation();
    void saveCurrentConversation() const;

    juce::File getCurrentHistoryFile() const;
    static juce::File getHistoryFileForProject (const juce::File& projectFile);
    static juce::File getUnsavedHistoryFile();
    static juce::File getUnsavedHistoryFile (const juce::String& sessionId);

    void handleProjectFileChangeForTesting (const juce::File& previousProjectFile,
                                            const juce::File& currentProjectFile,
                                            ProjectManager::FileChangeKind changeKind);

private:
    void loadConversationFromFile (const juce::File& historyFile);
    void projectDirtyChanged() override {}
    void projectFileChanged (const juce::File& previousProjectFile,
                             const juce::File& currentProjectFile,
                             ProjectManager::FileChangeKind changeKind) override;

    AiAgent& aiAgent;
    ProjectManager& projectManager;
    juce::String unsavedSessionId = juce::Uuid().toString();
    juce::File currentHistoryFile;
};

} // namespace waive
