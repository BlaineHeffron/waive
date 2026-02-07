#pragma once

#include <JuceHeader.h>

class UndoableCommandHandler;
class EditSession;
class ProjectManager;
class SessionComponent;
class ConsoleComponent;
class ToolLogComponent;
class LibraryComponent;
class PluginBrowserComponent;
class ToolSidebarComponent;

namespace waive { class JobQueue; class ToolRegistry; class ModelManager; }

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget
{
public:
    MainComponent (UndoableCommandHandler& handler, EditSession& session,
                   waive::JobQueue& jobQueue, ProjectManager& projectMgr);
    ~MainComponent() override;

    void resized() override;

    // Test helpers
    SessionComponent& getSessionComponentForTesting();
    ToolSidebarComponent& getToolSidebarForTesting();
    LibraryComponent& getLibraryComponentForTesting();
    PluginBrowserComponent& getPluginBrowserForTesting();
    bool invokeCommandForTesting (juce::CommandID commandID);

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const juce::ApplicationCommandTarget::InvocationInfo& info) override;

    enum CommandIDs
    {
        cmdUndo    = 0x2001,
        cmdRedo    = 0x2002,
        cmdNew     = 0x2003,
        cmdOpen    = 0x2004,
        cmdSave    = 0x2005,
        cmdSaveAs  = 0x2006,
        cmdDelete  = 0x2010,
        cmdDuplicate = 0x2011,
        cmdSplit   = 0x2012,
        cmdToggleToolSidebar = 0x2020,
        cmdPlay    = 0x2030,
        cmdStop    = 0x2031,
        cmdRecord  = 0x2032,
        cmdGoToStart = 0x2033,
        cmdAudioSettings = 0x2040,
        cmdRender  = 0x2041
    };

private:
    UndoableCommandHandler& commandHandler;
    EditSession& editSession;
    ProjectManager& projectManager;

    juce::ApplicationCommandManager commandManager;
    juce::MenuBarComponent menuBar;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<waive::ModelManager> modelManager;
    std::unique_ptr<waive::ToolRegistry> toolRegistry;
    std::unique_ptr<SessionComponent> sessionComponent;
    std::unique_ptr<LibraryComponent> libraryComponent;
    std::unique_ptr<PluginBrowserComponent> pluginBrowser;
    std::unique_ptr<ConsoleComponent> console;
    std::unique_ptr<ToolLogComponent> toolLog;
};
