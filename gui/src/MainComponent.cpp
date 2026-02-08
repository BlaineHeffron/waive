#include "MainComponent.h"

#include "UndoableCommandHandler.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "LibraryComponent.h"
#include "PluginBrowserComponent.h"
#include "ToolSidebarComponent.h"
#include "ConsoleComponent.h"
#include "ToolLogComponent.h"
#include "RenderDialog.h"
#include "AutoSaveManager.h"
#include "JobQueue.h"
#include "ModelManager.h"
#include "ToolRegistry.h"
#include "AiAgent.h"
#include "AiSettings.h"
#include "WaiveSpacing.h"
#include "ProjectPackager.h"

//==============================================================================
MainComponent::MainComponent (UndoableCommandHandler& handler, EditSession& session,
                              waive::JobQueue& jobQueue, ProjectManager& projectMgr,
                              waive::AiAgent* aiAgent, waive::AiSettings* aiSettings)
    : commandHandler (handler),
      editSession (session),
      projectManager (projectMgr)
{
    modelManager = std::make_unique<waive::ModelManager>();
    toolRegistry = std::make_unique<waive::ToolRegistry>();
    sessionComponent = std::make_unique<SessionComponent> (editSession, commandHandler,
                                                            toolRegistry.get(), modelManager.get(),
                                                            &jobQueue, &projectManager,
                                                            aiAgent, aiSettings);
    libraryComponent = std::make_unique<LibraryComponent> (editSession);
    pluginBrowser = std::make_unique<PluginBrowserComponent> (editSession, commandHandler);
    console = std::make_unique<ConsoleComponent> (commandHandler);
    toolLog = std::make_unique<ToolLogComponent> (jobQueue);
    autoSaveManager = std::make_unique<AutoSaveManager> (editSession, projectManager);

    auto tabBg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    tabs.addTab ("Session", tabBg, sessionComponent.get(), false);
    tabs.addTab ("Library", tabBg, libraryComponent.get(), false);
    tabs.addTab ("Plugins", tabBg, pluginBrowser.get(), false);
    tabs.addTab ("Console", tabBg, console.get(), false);
    tabs.addTab ("Tool Log", tabBg, toolLog.get(), false);

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

SessionComponent& MainComponent::getSessionComponentForTesting()
{
    return *sessionComponent;
}

ToolSidebarComponent& MainComponent::getToolSidebarForTesting()
{
    return *sessionComponent->getToolSidebar();
}

LibraryComponent& MainComponent::getLibraryComponentForTesting()
{
    return *libraryComponent;
}

PluginBrowserComponent& MainComponent::getPluginBrowserForTesting()
{
    return *pluginBrowser;
}

bool MainComponent::invokeCommandForTesting (juce::CommandID commandID)
{
    juce::ApplicationCommandTarget::InvocationInfo info (commandID);
    info.invocationMethod = juce::ApplicationCommandTarget::InvocationInfo::direct;
    return perform (info);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    menuBar.setBounds (bounds.removeFromTop (waive::Spacing::menuBarHeight));
    tabs.setBounds (bounds);
}

//==============================================================================
// MenuBarModel
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Transport", "View", "Help" };
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
        menu.addCommandItem (&commandManager, cmdAudioSettings);
        menu.addCommandItem (&commandManager, cmdRender);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdCollectAndSave);
        menu.addCommandItem (&commandManager, cmdRemoveUnusedMedia);
        menu.addCommandItem (&commandManager, cmdPackageAsZip);
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
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdDeleteTrack);
    }
    else if (menuIndex == 2) // Transport
    {
        menu.addCommandItem (&commandManager, cmdPlay);
        menu.addCommandItem (&commandManager, cmdStop);
        menu.addCommandItem (&commandManager, cmdRecord);
        menu.addCommandItem (&commandManager, cmdGoToStart);
    }
    else if (menuIndex == 3) // View
    {
        menu.addCommandItem (&commandManager, cmdToggleToolSidebar);
        menu.addCommandItem (&commandManager, cmdToggleChatPanel);
    }
    else if (menuIndex == 4) // Help
    {
        menu.addCommandItem (&commandManager, cmdShowShortcuts);
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
    commands.add (cmdDeleteTrack);
    commands.add (cmdToggleToolSidebar);
    commands.add (cmdToggleChatPanel);
    commands.add (cmdShowShortcuts);
    commands.add (cmdPlay);
    commands.add (cmdStop);
    commands.add (cmdRecord);
    commands.add (cmdGoToStart);
    commands.add (cmdAudioSettings);
    commands.add (cmdRender);
    commands.add (cmdCollectAndSave);
    commands.add (cmdRemoveUnusedMedia);
    commands.add (cmdPackageAsZip);
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
            result.setActive (sessionComponent->getTimeline().getSelectionManager().getSelectedClips().size() > 0);
            break;
        case cmdDuplicate:
            result.setInfo ("Duplicate", "Duplicate selected clips", "Edit", 0);
            result.addDefaultKeypress ('d', juce::ModifierKeys::commandModifier);
            result.setActive (sessionComponent->getTimeline().getSelectionManager().getSelectedClips().size() > 0);
            break;
        case cmdSplit:
            result.setInfo ("Split at Playhead", "Split selected clips at playhead", "Edit", 0);
            result.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier);
            result.setActive (sessionComponent->getTimeline().getSelectionManager().getSelectedClips().size() > 0);
            break;
        case cmdDeleteTrack:
            result.setInfo ("Delete Track", "Delete the selected track", "Edit", 0);
            result.addDefaultKeypress (juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier);
            break;
        case cmdToggleToolSidebar:
            result.setInfo ("Toggle Tool Sidebar", "Show or hide the tool sidebar", "View", 0);
            result.addDefaultKeypress ('t', juce::ModifierKeys::commandModifier);
            break;
        case cmdToggleChatPanel:
            result.setInfo ("AI Chat", "Show or hide the AI chat panel", "View", 0);
            result.addDefaultKeypress ('c', juce::ModifierKeys::commandModifier
                                                | juce::ModifierKeys::shiftModifier);
            break;
        case cmdShowShortcuts:
            result.setInfo ("Keyboard Shortcuts...", "Show keyboard shortcuts reference", "Help", 0);
            result.addDefaultKeypress ('/', juce::ModifierKeys::commandModifier);
            break;
        case cmdPlay:
            result.setInfo ("Play", "Start or stop playback", "Transport", 0);
            result.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            break;
        case cmdStop:
            result.setInfo ("Stop", "Stop playback", "Transport", 0);
            break;
        case cmdRecord:
            result.setInfo ("Record", "Toggle recording", "Transport", 0);
            result.addDefaultKeypress ('r', 0);
            break;
        case cmdGoToStart:
            result.setInfo ("Go to Start", "Jump to start of timeline", "Transport", 0);
            result.addDefaultKeypress (juce::KeyPress::homeKey, 0);
            break;
        case cmdAudioSettings:
            result.setInfo ("Audio Settings...", "Configure audio device settings", "File", 0);
            break;
        case cmdRender:
            result.setInfo ("Render...", "Render project to audio file", "File", 0);
            break;
        case cmdCollectAndSave:
            result.setInfo ("Collect and Save...", "Copy external media into project folder", "File", 0);
            break;
        case cmdRemoveUnusedMedia:
            result.setInfo ("Remove Unused Media...", "Remove orphaned audio files", "File", 0);
            break;
        case cmdPackageAsZip:
            result.setInfo ("Package as Zip...", "Create portable zip archive", "File", 0);
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
        {
            // Warn about unsaved changes in current project
            if (editSession.hasChangedSinceSaved())
            {
                bool shouldDiscard = juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::WarningIcon,
                    "Unsaved Changes",
                    "The current project has unsaved changes. Create new project anyway?",
                    "Discard Changes", "Cancel");

                if (! shouldDiscard)
                    return true;
            }
            projectManager.newProject();
            return true;
        }
        case cmdOpen:
        {
            // Check for auto-save before opening
            juce::FileChooser chooser ("Open Project...", juce::File(), "*.tracktionedit");
            if (chooser.browseForFileToOpen())
            {
                auto file = chooser.getResult();
                auto autoSaveFile = AutoSaveManager::checkForAutoSave (file);

                if (autoSaveFile != juce::File())
                {
                    bool shouldRecover = juce::AlertWindow::showOkCancelBox (
                        juce::MessageBoxIconType::QuestionIcon,
                        "Recover Unsaved Changes?",
                        "An auto-save file was found. Would you like to recover unsaved changes?",
                        "Recover", "Discard");

                    if (shouldRecover)
                        projectManager.openProject (autoSaveFile);
                    else
                    {
                        AutoSaveManager::deleteAutoSave (file);
                        projectManager.openProject (file);
                    }
                }
                else
                {
                    projectManager.openProject (file);
                }
            }
            return true;
        }
        case cmdSave:
        {
            bool success = projectManager.save();
            if (success)
                AutoSaveManager::deleteAutoSave (projectManager.getCurrentFile());
            return true;
        }
        case cmdSaveAs:
        {
            bool success = projectManager.saveAs();
            if (success)
                AutoSaveManager::deleteAutoSave (projectManager.getCurrentFile());
            return true;
        }
        case cmdDelete:
            sessionComponent->getTimeline().deleteSelectedClips();
            return true;
        case cmdDuplicate:
            sessionComponent->getTimeline().duplicateSelectedClips();
            return true;
        case cmdSplit:
            sessionComponent->getTimeline().splitSelectedClipsAtPlayhead();
            return true;
        case cmdDeleteTrack:
        {
            // Find first selected track via selected clips
            auto& selMgr = sessionComponent->getTimeline().getSelectionManager();
            auto selectedClips = selMgr.getSelectedClips();

            te::AudioTrack* selectedTrack = nullptr;
            if (selectedClips.size() > 0)
            {
                if (auto* clip = selectedClips.getFirst())
                    selectedTrack = dynamic_cast<te::AudioTrack*> (clip->getTrack());
            }

            if (selectedTrack == nullptr)
            {
                // If no clips selected, try to find first track
                auto& edit = editSession.getEdit();
                for (auto* track : edit.getTrackList())
                {
                    if (auto* audioTrack = dynamic_cast<te::AudioTrack*> (track))
                    {
                        selectedTrack = audioTrack;
                        break;
                    }
                }
            }

            if (selectedTrack != nullptr)
            {
                bool hasClips = selectedTrack->getClips().size() > 0;
                if (hasClips)
                {
                    // Capture track itemID to safely locate track in async callback
                    auto trackID = selectedTrack->itemID;
                    juce::AlertWindow::showOkCancelBox (
                        juce::MessageBoxIconType::WarningIcon,
                        "Delete Track",
                        "This track contains " + juce::String (selectedTrack->getClips().size()) + " clip(s). Are you sure?",
                        "Delete", "Cancel",
                        nullptr,
                        juce::ModalCallbackFunction::create ([this, trackID] (int choice)
                        {
                            if (choice == 1)
                            {
                                auto& edit = editSession.getEdit();
                                for (auto* track : edit.getTrackList())
                                {
                                    if (track->itemID == trackID)
                                    {
                                        editSession.performEdit ("Delete Track", [track] (te::Edit& ed)
                                        {
                                            ed.deleteTrack (track);
                                        });
                                        break;
                                    }
                                }
                            }
                        })
                    );
                }
                else
                {
                    editSession.performEdit ("Delete Track", [selectedTrack] (te::Edit& edit)
                    {
                        edit.deleteTrack (selectedTrack);
                    });
                }
            }
            return true;
        }
        case cmdToggleToolSidebar:
            sessionComponent->toggleToolSidebar();
            return true;
        case cmdToggleChatPanel:
            sessionComponent->toggleChatPanel();
            return true;
        case cmdPlay:
            sessionComponent->play();
            return true;
        case cmdStop:
            sessionComponent->stop();
            return true;
        case cmdRecord:
            sessionComponent->record();
            return true;
        case cmdGoToStart:
            sessionComponent->goToStart();
            return true;
        case cmdAudioSettings:
        {
            auto& engine = editSession.getEdit().engine;
            auto& deviceManager = engine.getDeviceManager();

            juce::DialogWindow::LaunchOptions opts;
            auto* selector = new juce::AudioDeviceSelectorComponent (
                deviceManager.deviceManager,
                0, 256, 0, 256, false, false, true, false);

            opts.content.setOwned (selector);
            opts.dialogTitle = "Audio Settings";
            opts.dialogBackgroundColour = juce::Colours::darkgrey;
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = false;
            opts.runModal();
            return true;
        }
        case cmdRender:
        {
            auto* renderDialog = new RenderDialog (editSession, commandHandler);
            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned (renderDialog);
            opts.dialogTitle = "Render Audio";
            opts.dialogBackgroundColour = juce::Colours::darkgrey;
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = false;
            opts.runModal();
            return true;
        }
        case cmdCollectAndSave:
        {
            auto currentFile = projectManager.getCurrentFile();
            if (currentFile == juce::File())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "No Project",
                    "Please save the project first before collecting media.");
                return true;
            }

            auto projectDir = currentFile.getParentDirectory();
            auto& edit = editSession.getEdit();
            auto result = waive::ProjectPackager::collectAndSave (edit, projectDir);

            juce::String message;
            if (result.filesCopied > 0)
            {
                message << "Collected " << result.filesCopied << " file(s)\n";
                message << "Total: " << juce::File::descriptionOfSizeInBytes (result.bytesCopied);
            }
            else
            {
                message << "No external media found.";
            }

            if (result.errors.size() > 0)
            {
                message << "\n\nErrors:\n" << result.errors.joinIntoString ("\n");
            }

            juce::AlertWindow::showMessageBoxAsync (
                result.errors.isEmpty() ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                "Collect and Save",
                message);
            return true;
        }
        case cmdRemoveUnusedMedia:
        {
            auto currentFile = projectManager.getCurrentFile();
            if (currentFile == juce::File())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "No Project",
                    "Please save the project first.");
                return true;
            }

            auto projectDir = currentFile.getParentDirectory();
            auto& edit = editSession.getEdit();
            auto unusedFiles = waive::ProjectPackager::findUnusedMedia (edit, projectDir);

            if (unusedFiles.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::InfoIcon,
                    "Remove Unused Media",
                    "No unused media files found.");
                return true;
            }

            juce::String message;
            message << "Found " << unusedFiles.size() << " unused file(s):\n\n";
            for (const auto& file : unusedFiles)
                message << "  " << file.getFileName() << "\n";
            message << "\nFiles will be moved to .trash folder.\nRemove these files?";

            bool shouldRemove = juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Remove Unused Media",
                message,
                "Remove", "Cancel");

            if (shouldRemove)
            {
                int removed = waive::ProjectPackager::removeUnusedMedia (edit, projectDir);
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::InfoIcon,
                    "Remove Unused Media",
                    "Removed " + juce::String (removed) + " file(s) to .trash folder.");
            }
            return true;
        }
        case cmdPackageAsZip:
        {
            auto currentFile = projectManager.getCurrentFile();
            if (currentFile == juce::File())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "No Project",
                    "Please save the project first before packaging.");
                return true;
            }

            juce::FileChooser chooser ("Package as Zip...",
                                        currentFile.getParentDirectory(),
                                        "*.zip");
            if (chooser.browseForFileToSave (true))
            {
                auto outputZip = chooser.getResult();
                auto projectDir = currentFile.getParentDirectory();
                auto& edit = editSession.getEdit();

                // First collect external media
                auto collectResult = waive::ProjectPackager::collectAndSave (edit, projectDir);

                // Then create zip
                bool success = waive::ProjectPackager::packageAsZip (projectDir, outputZip);

                juce::String message;
                if (success)
                {
                    message << "Successfully packaged project to:\n" << outputZip.getFullPathName();
                    if (collectResult.filesCopied > 0)
                    {
                        message << "\n\nCollected " << collectResult.filesCopied << " external file(s).";
                    }
                }
                else
                {
                    message << "Failed to create zip archive.";
                }

                juce::AlertWindow::showMessageBoxAsync (
                    success ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                    "Package as Zip",
                    message);
            }
            return true;
        }
        case cmdShowShortcuts:
        {
            juce::String shortcutsText;
            shortcutsText << "KEYBOARD SHORTCUTS\n\n";
            shortcutsText << "File:\n";
            shortcutsText << "  New                 Cmd+N\n";
            shortcutsText << "  Open                Cmd+O\n";
            shortcutsText << "  Save                Cmd+S\n";
            shortcutsText << "  Save As             Cmd+Shift+S\n\n";
            shortcutsText << "Edit:\n";
            shortcutsText << "  Undo                Cmd+Z\n";
            shortcutsText << "  Redo                Cmd+Shift+Z\n";
            shortcutsText << "  Delete              Delete\n";
            shortcutsText << "  Duplicate           Cmd+D\n";
            shortcutsText << "  Split at Playhead   Cmd+E\n";
            shortcutsText << "  Delete Track        Cmd+Backspace\n\n";
            shortcutsText << "Transport:\n";
            shortcutsText << "  Play/Stop           Space\n";
            shortcutsText << "  Record              R\n";
            shortcutsText << "  Go to Start         Home\n\n";
            shortcutsText << "View:\n";
            shortcutsText << "  Toggle Tool Sidebar Cmd+T\n";
            shortcutsText << "  AI Chat             Cmd+Shift+C\n\n";
            shortcutsText << "Help:\n";
            shortcutsText << "  Keyboard Shortcuts  Cmd+/\n";

            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::InfoIcon,
                "Keyboard Shortcuts",
                shortcutsText);
            return true;
        }
        default:
            return false;
    }
}
