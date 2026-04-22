#include "ProjectChatHistoryController.h"

#include "AiAgent.h"

namespace waive
{

ProjectChatHistoryController::ProjectChatHistoryController (AiAgent& agent,
                                                            ProjectManager& manager)
    : aiAgent (agent),
      projectManager (manager),
      currentHistoryFile (manager.getCurrentFile() != juce::File()
                            ? getHistoryFileForProject (manager.getCurrentFile())
                            : getUnsavedHistoryFile (unsavedSessionId))
{
    projectManager.addListener (this);
}

ProjectChatHistoryController::~ProjectChatHistoryController()
{
    projectManager.removeListener (this);
}

void ProjectChatHistoryController::loadCurrentConversation()
{
    if (projectManager.getCurrentFile() != juce::File())
        loadConversationFromFile (getHistoryFileForProject (projectManager.getCurrentFile()));
    else
        loadConversationFromFile (getUnsavedHistoryFile (unsavedSessionId));
}

void ProjectChatHistoryController::saveCurrentConversation() const
{
    aiAgent.saveConversation (currentHistoryFile);
}

juce::File ProjectChatHistoryController::getCurrentHistoryFile() const
{
    return currentHistoryFile;
}

juce::File ProjectChatHistoryController::getHistoryFileForProject (const juce::File& projectFile)
{
    if (projectFile != juce::File())
    {
        auto projectDir = projectFile.getParentDirectory();
        auto chatDir = projectDir.getChildFile (".waive_chat");
        return chatDir.getChildFile (projectFile.getFileNameWithoutExtension() + ".chat.json");
    }

    return getUnsavedHistoryFile();
}

juce::File ProjectChatHistoryController::getUnsavedHistoryFile()
{
    return getUnsavedHistoryFile ("default");
}

juce::File ProjectChatHistoryController::getUnsavedHistoryFile (const juce::String& sessionId)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Waive")
               .getChildFile ("chat_history")
               .getChildFile ("unsaved_" + (sessionId.isNotEmpty() ? sessionId : "default") + ".json");
}

void ProjectChatHistoryController::loadConversationFromFile (const juce::File& historyFile)
{
    currentHistoryFile = historyFile;
    aiAgent.clearConversation();
    aiAgent.loadConversation (historyFile);
}

void ProjectChatHistoryController::projectFileChanged (const juce::File& previousProjectFile,
                                                       const juce::File& currentProjectFile,
                                                       ProjectManager::FileChangeKind changeKind)
{
    if (currentProjectFile == juce::File()
        && changeKind != ProjectManager::FileChangeKind::save
        && changeKind != ProjectManager::FileChangeKind::saveAs)
        unsavedSessionId = juce::Uuid().toString();

    const auto previousHistoryFile = previousProjectFile != juce::File()
                                       ? getHistoryFileForProject (previousProjectFile)
                                       : currentHistoryFile;
    const auto nextHistoryFile = currentProjectFile != juce::File()
                                   ? getHistoryFileForProject (currentProjectFile)
                                   : getUnsavedHistoryFile (unsavedSessionId);

    aiAgent.saveConversation (previousHistoryFile);

    if (changeKind == ProjectManager::FileChangeKind::save)
    {
        currentHistoryFile = nextHistoryFile;
        return;
    }

    if (changeKind == ProjectManager::FileChangeKind::saveAs)
    {
        aiAgent.saveConversation (nextHistoryFile);
        if (previousProjectFile == juce::File())
            (void) previousHistoryFile.deleteFile();
        currentHistoryFile = nextHistoryFile;
        return;
    }

    loadConversationFromFile (nextHistoryFile);
}

} // namespace waive
