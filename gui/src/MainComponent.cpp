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
#include "UiMessageHelpers.h"

#include <cstdlib>

namespace
{
juce::Array<juce::File> makeAllowedMediaDirectories (ProjectManager& projectManager,
                                                     const waive::ModelManager* modelManager)
{
    juce::Array<juce::File> directories;
    directories.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    auto projectFile = projectManager.getCurrentFile();
    if (projectFile != juce::File())
        directories.addIfNotAlreadyThere (projectFile.getParentDirectory());

    if (modelManager != nullptr)
        directories.addIfNotAlreadyThere (modelManager->getStorageDirectory());

    return directories;
}

bool isHeadlessUiEnvironment()
{
   #if JUCE_LINUX || JUCE_BSD
    const auto hasDisplayEnv = [] (const char* name)
    {
        if (const auto* value = std::getenv (name))
            return *value != '\0';

        return false;
    };

    return ! hasDisplayEnv ("DISPLAY") && ! hasDisplayEnv ("WAYLAND_DISPLAY");
   #else
    return false;
   #endif
}

te::AudioTrack* getSelectedAudioTrack (SessionComponent* sessionComponent)
{
    if (sessionComponent == nullptr)
        return nullptr;

    auto selectedClips = sessionComponent->getTimeline().getSelectionManager().getSelectedClips();
    if (selectedClips.isEmpty())
        return nullptr;

    if (auto* clip = selectedClips.getFirst())
        return dynamic_cast<te::AudioTrack*> (clip->getTrack());

    return nullptr;
}

te::AudioTrack* getSelectedAudioTrack (SessionComponent* sessionComponent,
                                       PluginBrowserComponent* pluginBrowser)
{
    if (auto* track = getSelectedAudioTrack (sessionComponent))
        return track;

    if (pluginBrowser != nullptr)
        return pluginBrowser->getSelectedTrackForTesting();

    return nullptr;
}

juce::String getShortcutModifierLabel()
{
   #if JUCE_MAC
    return "Cmd";
   #else
    return "Ctrl";
   #endif
}

}

//==============================================================================
MainComponent::MainComponent (UndoableCommandHandler& handler, EditSession& session,
                              waive::JobQueue& jobQueue, ProjectManager& projectMgr,
                              waive::AiAgent* aiAgent, waive::AiSettings* aiSettings)
    : commandHandler (handler),
      editSession (session),
      projectManager (projectMgr)
{
    ownedToolRegistry = std::make_unique<waive::ToolRegistry>();
    ownedModelManager = std::make_unique<waive::ModelManager>();
    toolRegistry = ownedToolRegistry.get();
    modelManager = ownedModelManager.get();
    initialiseUi (jobQueue, aiAgent, aiSettings);
}

MainComponent::MainComponent (UndoableCommandHandler& handler, EditSession& session,
                              waive::JobQueue& jobQueue, ProjectManager& projectMgr,
                              waive::ToolRegistry& registry, waive::ModelManager& models,
                              waive::AiAgent* aiAgent, waive::AiSettings* aiSettings)
    : commandHandler (handler),
      editSession (session),
      projectManager (projectMgr),
      toolRegistry (&registry),
      modelManager (&models)
{
    initialiseUi (jobQueue, aiAgent, aiSettings);
}

void MainComponent::initialiseUi (waive::JobQueue& jobQueue, waive::AiAgent* aiAgent, waive::AiSettings* aiSettings)
{
    auto refreshAllowedMediaDirectories = [this]
    {
        commandHandler.setAllowedMediaDirectories (makeAllowedMediaDirectories (projectManager, modelManager));
    };

    sessionComponent = std::make_unique<SessionComponent> (editSession, commandHandler,
                                                            toolRegistry, modelManager,
                                                            &jobQueue, &projectManager,
                                                            refreshAllowedMediaDirectories,
                                                            aiAgent, aiSettings);
    libraryComponent = std::make_unique<LibraryComponent> (editSession);
    pluginBrowser = std::make_unique<PluginBrowserComponent> (editSession, commandHandler);
    console = std::make_unique<ConsoleComponent> (commandHandler);
    toolLog = std::make_unique<ToolLogComponent> (jobQueue);
    autoSaveManager = std::make_unique<AutoSaveManager> (editSession, projectManager,
                                                         AutoSaveManager::getConfiguredIntervalSeconds());

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
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 500);
    refreshAllowedMediaDirectories();

    // Allow key commands
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);
}

MainComponent::~MainComponent()
{
    if (auto* modalManager = juce::ModalComponentManager::getInstanceWithoutCreating())
        modalManager->cancelAllModalComponents();

    tabs.clearTabs();
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

ConsoleComponent& MainComponent::getConsoleForTesting()
{
    return *console;
}

bool MainComponent::invokeCommandForTesting (juce::CommandID commandID)
{
    juce::ApplicationCommandTarget::InvocationInfo info (commandID);
    info.invocationMethod = juce::ApplicationCommandTarget::InvocationInfo::direct;
    return perform (info);
}

void MainComponent::markCleanShutdown()
{
    if (autoSaveManager != nullptr)
        autoSaveManager->markCleanShutdown();
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
            result.setActive (getSelectedAudioTrack (sessionComponent.get(), pluginBrowser.get()) != nullptr);
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
            projectManager.newProject();
            return true;
        case cmdOpen:
        {
            juce::FileChooser chooser ("Open Project...", juce::File(), "*.tracktionedit");
            if (chooser.browseForFileToOpen())
                return projectManager.openProject (chooser.getResult());
            return false;
        }
        case cmdSave:
        {
            bool success = projectManager.save();
            if (success)
                AutoSaveManager::deleteAutoSave (projectManager.getCurrentFile());
            return success;
        }
        case cmdSaveAs:
        {
            bool success = projectManager.saveAs();
            if (success)
                AutoSaveManager::deleteAutoSave (projectManager.getCurrentFile());
            return success;
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
            auto* selectedTrack = getSelectedAudioTrack (sessionComponent.get(), pluginBrowser.get());

            if (selectedTrack != nullptr)
            {
                bool hasClips = selectedTrack->getClips().size() > 0;
                if (hasClips)
                {
                    // Capture track itemID to safely locate track in async callback
                    auto trackID = selectedTrack->itemID;
                    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
                    if (! waive::showOkCancelBoxSafe (
                            juce::MessageBoxIconType::WarningIcon,
                            "Delete Track",
                            "This track contains " + juce::String (selectedTrack->getClips().size()) + " clip(s). Are you sure?",
                            "Delete", "Cancel"))
                    {
                        return true;
                    }

                    if (safeThis == nullptr)
                        return true;

                    auto& edit = safeThis->editSession.getEdit();
                    for (auto* track : edit.getTrackList())
                    {
                        if (track->itemID == trackID)
                        {
                            safeThis->editSession.performEdit ("Delete Track", [track] (te::Edit& ed)
                            {
                                ed.deleteTrack (track);
                            });
                            break;
                        }
                    }
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
            if (isHeadlessUiEnvironment())
            {
                juce::Logger::writeToLog ("Skipping audio settings dialog: no display server available");
                return true;
            }

            auto& engine = editSession.getEdit().engine;
            auto& deviceManager = engine.getDeviceManager();

            juce::DialogWindow::LaunchOptions opts;
            auto* selector = new juce::AudioDeviceSelectorComponent (
                deviceManager.deviceManager,
                0, 256, 0, 256, false, false, true, false);

            opts.content.setOwned (selector);
            opts.dialogTitle = "Audio Settings";
            opts.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = false;
            opts.runModal();
            return true;
        }
        case cmdRender:
        {
            if (isHeadlessUiEnvironment())
            {
                juce::Logger::writeToLog ("Skipping render dialog: no display server available");
                return true;
            }

            auto* renderDialog = new RenderDialog (editSession, commandHandler);
            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned (renderDialog);
            opts.dialogTitle = "Render Audio";
            opts.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
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
                waive::showMessageBoxAsyncSafe (
                    juce::AlertWindow::WarningIcon,
                    "No Project",
                    "Please save the project first before collecting media.");
                return true;
            }

            auto projectDir = currentFile.getParentDirectory();
            auto& edit = editSession.getEdit();
            auto result = waive::ProjectPackager::collectAndSave (edit, projectDir, currentFile);

            if (result.errors.isEmpty())
                projectManager.markCurrentProjectSaved();

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

            waive::showMessageBoxAsyncSafe (
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
                waive::showMessageBoxAsyncSafe (
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
                waive::showMessageBoxAsyncSafe (
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

            bool shouldRemove = waive::showOkCancelBoxSafe (
                juce::MessageBoxIconType::QuestionIcon,
                "Remove Unused Media",
                message,
                "Remove", "Cancel");

            if (shouldRemove)
            {
                auto removeResult = waive::ProjectPackager::removeUnusedMedia (edit, projectDir);
                juce::String resultMessage = "Removed " + juce::String (removeResult.filesRemoved)
                                            + " file(s) to .trash folder.";
                if (removeResult.errors.isEmpty())
                    resultMessage << "\nFreed " << juce::File::descriptionOfSizeInBytes (removeResult.bytesFreed) << ".";
                else
                    resultMessage << "\nSome files could not be moved:\n"
                                  << removeResult.errors.joinIntoString ("\n");
                waive::showMessageBoxAsyncSafe (
                    removeResult.errors.isEmpty() ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                    "Remove Unused Media",
                    resultMessage);
            }
            return true;
        }
        case cmdPackageAsZip:
        {
            auto currentFile = projectManager.getCurrentFile();
            if (currentFile == juce::File())
            {
                waive::showMessageBoxAsyncSafe (
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
                auto collectResult = waive::ProjectPackager::collectAndSave (edit, projectDir, currentFile);
                if (collectResult.errors.isEmpty())
                    projectManager.markCurrentProjectSaved();

                // Then create zip
                bool success = collectResult.errors.isEmpty()
                               && waive::ProjectPackager::packageAsZip (currentFile, outputZip);

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
                    if (! collectResult.errors.isEmpty())
                        message << "\n\nCollect errors:\n" << collectResult.errors.joinIntoString ("\n");
                }

                waive::showMessageBoxAsyncSafe (
                    success ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                    "Package as Zip",
                    message);
            }
            return true;
        }
        case cmdShowShortcuts:
        {
            juce::String shortcutsText;
            const auto mod = getShortcutModifierLabel();
            shortcutsText << "KEYBOARD SHORTCUTS\n\n";
            shortcutsText << "File:\n";
            shortcutsText << "  New                 " << mod << "+N\n";
            shortcutsText << "  Open                " << mod << "+O\n";
            shortcutsText << "  Save                " << mod << "+S\n";
            shortcutsText << "  Save As             " << mod << "+Shift+S\n\n";
            shortcutsText << "Edit:\n";
            shortcutsText << "  Undo                " << mod << "+Z\n";
            shortcutsText << "  Redo                " << mod << "+Shift+Z\n";
            shortcutsText << "  Delete              Delete\n";
            shortcutsText << "  Duplicate           " << mod << "+D\n";
            shortcutsText << "  Split at Playhead   " << mod << "+E\n";
            shortcutsText << "  Delete Track        " << mod << "+Backspace\n\n";
            shortcutsText << "Transport:\n";
            shortcutsText << "  Play/Stop           Space\n";
            shortcutsText << "  Record              R\n";
            shortcutsText << "  Go to Start         Home\n\n";
            shortcutsText << "View:\n";
            shortcutsText << "  Toggle Tool Sidebar " << mod << "+T\n";
            shortcutsText << "  AI Chat             " << mod << "+Shift+C\n\n";
            shortcutsText << "Help:\n";
            shortcutsText << "  Keyboard Shortcuts  " << mod << "+/\n";

            waive::showMessageBoxAsyncSafe (
                juce::AlertWindow::InfoIcon,
                "Keyboard Shortcuts",
                shortcutsText);
            return true;
        }
        default:
            return false;
    }
}
