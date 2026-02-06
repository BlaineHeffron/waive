#include "MainComponent.h"

#include "UndoableCommandHandler.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "SessionComponent.h"
#include "LibraryComponent.h"
#include "PluginBrowserComponent.h"
#include "ConsoleComponent.h"
#include "ToolLogComponent.h"
#include "JobQueue.h"

//==============================================================================
MainComponent::MainComponent (UndoableCommandHandler& handler, EditSession& session,
                              waive::JobQueue& jobQueue, ProjectManager& projectMgr)
    : commandHandler (handler),
      editSession (session),
      projectManager (projectMgr)
{
    sessionComponent = std::make_unique<SessionComponent> (editSession, commandHandler);
    libraryComponent = std::make_unique<LibraryComponent> (editSession);
    pluginBrowser = std::make_unique<PluginBrowserComponent> (editSession, commandHandler);
    console = std::make_unique<ConsoleComponent> (commandHandler);
    toolLog = std::make_unique<ToolLogComponent> (jobQueue);

    tabs.addTab ("Session", juce::Colours::darkgrey, sessionComponent.get(), false);
    tabs.addTab ("Library", juce::Colours::darkgrey, libraryComponent.get(), false);
    tabs.addTab ("Plugins", juce::Colours::darkgrey, pluginBrowser.get(), false);
    tabs.addTab ("Console", juce::Colours::darkgrey, console.get(), false);
    tabs.addTab ("Tool Log", juce::Colours::darkgrey, toolLog.get(), false);

    // Set up menu bar
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    menuBar.setModel (this);
    addAndMakeVisible (menuBar);

    addAndMakeVisible (tabs);

    // Allow key commands
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);
}

MainComponent::~MainComponent()
{
    menuBar.setModel (nullptr);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    menuBar.setBounds (bounds.removeFromTop (24));
    tabs.setBounds (bounds);
}

//==============================================================================
// MenuBarModel
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0) // File
    {
        menu.addCommandItem (&commandManager, cmdNew);
        menu.addCommandItem (&commandManager, cmdOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdSave);
        menu.addCommandItem (&commandManager, cmdSaveAs);

        // Recent files submenu
        auto recentFiles = projectManager.getRecentFiles();
        if (recentFiles.size() > 0)
        {
            juce::PopupMenu recentMenu;
            for (int i = 0; i < recentFiles.size(); ++i)
            {
                juce::File f (recentFiles[i]);
                recentMenu.addItem (10000 + i, f.getFileName());
            }
            recentMenu.addSeparator();
            recentMenu.addItem (10999, "Clear Recent Files");
            menu.addSeparator();
            menu.addSubMenu ("Recent Files", recentMenu);
        }

        menu.addSeparator();
        menu.addItem (9999, "Quit");
    }
    else if (menuIndex == 1) // Edit
    {
        menu.addCommandItem (&commandManager, cmdUndo);
        menu.addCommandItem (&commandManager, cmdRedo);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdDelete);
        menu.addCommandItem (&commandManager, cmdDuplicate);
        menu.addCommandItem (&commandManager, cmdSplit);
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == 9999)
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
        return;
    }

    if (menuItemID == 10999)
    {
        projectManager.clearRecentFiles();
        return;
    }

    if (menuItemID >= 10000 && menuItemID < 10999)
    {
        auto recentFiles = projectManager.getRecentFiles();
        int index = menuItemID - 10000;
        if (index < recentFiles.size())
            projectManager.openProject (juce::File (recentFiles[index]));
    }
}

//==============================================================================
// ApplicationCommandTarget
//==============================================================================

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return nullptr;
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (cmdUndo);
    commands.add (cmdRedo);
    commands.add (cmdNew);
    commands.add (cmdOpen);
    commands.add (cmdSave);
    commands.add (cmdSaveAs);
    commands.add (cmdDelete);
    commands.add (cmdDuplicate);
    commands.add (cmdSplit);
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case cmdUndo:
        {
            result.setInfo ("Undo",
                            editSession.canUndo() ? "Undo " + editSession.getUndoDescription()
                                                  : "Nothing to undo",
                            "Edit", 0);
            result.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            result.setActive (editSession.canUndo());
            break;
        }
        case cmdRedo:
        {
            result.setInfo ("Redo",
                            editSession.canRedo() ? "Redo " + editSession.getRedoDescription()
                                                  : "Nothing to redo",
                            "Edit", 0);
            result.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier
                                                | juce::ModifierKeys::shiftModifier);
            result.setActive (editSession.canRedo());
            break;
        }
        case cmdNew:
            result.setInfo ("New", "Create a new project", "File", 0);
            result.addDefaultKeypress ('n', juce::ModifierKeys::commandModifier);
            break;
        case cmdOpen:
            result.setInfo ("Open...", "Open an existing project", "File", 0);
            result.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
            break;
        case cmdSave:
            result.setInfo ("Save", "Save the current project", "File", 0);
            result.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
            break;
        case cmdSaveAs:
            result.setInfo ("Save As...", "Save the project to a new file", "File", 0);
            result.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier
                                                | juce::ModifierKeys::shiftModifier);
            break;
        case cmdDelete:
            result.setInfo ("Delete", "Delete selected clips", "Edit", 0);
            result.addDefaultKeypress (juce::KeyPress::deleteKey, 0);
            break;
        case cmdDuplicate:
            result.setInfo ("Duplicate", "Duplicate selected clips", "Edit", 0);
            result.addDefaultKeypress ('d', juce::ModifierKeys::commandModifier);
            break;
        case cmdSplit:
            result.setInfo ("Split at Playhead", "Split selected clips at playhead", "Edit", 0);
            result.addDefaultKeypress ('s', 0);  // just 's' key
            break;
        default:
            break;
    }
}

bool MainComponent::perform (const juce::ApplicationCommandTarget::InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdUndo:
            editSession.undo();
            return true;
        case cmdRedo:
            editSession.redo();
            return true;
        case cmdNew:
            projectManager.newProject();
            return true;
        case cmdOpen:
            projectManager.openProject();
            return true;
        case cmdSave:
            projectManager.save();
            return true;
        case cmdSaveAs:
            projectManager.saveAs();
            return true;
        // Delete/Duplicate/Split will be routed to timeline once it exists
        case cmdDelete:
        case cmdDuplicate:
        case cmdSplit:
            return true;
        default:
            return false;
    }
}
