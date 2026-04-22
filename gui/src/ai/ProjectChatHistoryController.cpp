#include "ProjectChatHistoryController.h"

#include "AiAgent.h"

namespace waive
{

ProjectChatHistoryController::ProjectChatHistoryController (AiAgent& agent,
                                                            ProjectManager& manager)
    : aiAgent (agent),
      projectManager (manager),
      currentHistoryFile (getHistoryFileForProject (manager.getCurrentFile()))
{
    projectManager.addListener (this);
}

ProjectChatHistoryController::~ProjectChatHistoryController()
{
    projectManager.removeListener (this);
}

void ProjectChatHistoryController::loadCurrentConversation()
{
    loadConversationFromFile (getHistoryFileForProject (projectManager.getCurrentFile()));
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
    if (projectFile.existsAsFile())
    {
        auto projectDir = projectFile.getParentDirectory();
        auto chatDir = projectDir.getChildFile (".waive_chat");
        return chatDir.getChildFile (projectFile.getFileNameWithoutExtension() + ".chat.json");
    }

    return getUnsavedHistoryFile();
}

juce::File ProjectChatHistoryController::getUnsavedHistoryFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Waive")
               .getChildFile ("chat_history.json");
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
    const auto previousHistoryFile = getHistoryFileForProject (previousProjectFile);
    const auto nextHistoryFile = getHistoryFileForProject (currentProjectFile);

    aiAgent.saveConversation (previousHistoryFile);

    if (changeKind == ProjectManager::FileChangeKind::save)
    {
        currentHistoryFile = nextHistoryFile;
        return;
    }

    if (changeKind == ProjectManager::FileChangeKind::saveAs)
    {
        aiAgent.saveConversation (nextHistoryFile);
        currentHistoryFile = nextHistoryFile;
        return;
    }

    loadConversationFromFile (nextHistoryFile);
}

} // namespace waive
