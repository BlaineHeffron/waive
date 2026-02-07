#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "MainComponent.h"
#include "CommandHandler.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "ProjectManager.h"
#include "JobQueue.h"
#include "ToolRegistry.h"
#include "ExternalToolRunner.h"
#include "WaiveLookAndFeel.h"
#include "AiSettings.h"
#include "AiAgent.h"
#include "Tool.h"

namespace te = tracktion;

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::String name, UndoableCommandHandler& handler,
                EditSession& session, waive::JobQueue& queue,
                ProjectManager& projectMgr,
                waive::AiAgent* aiAgent = nullptr,
                waive::AiSettings* aiSettings = nullptr)
        : DocumentWindow (std::move (name),
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (handler, session, queue, projectMgr, aiAgent, aiSettings), true);
        centreWithSize (1200, 800);
        setResizeLimits (1024, 600, 4096, 2160);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

//==============================================================================
class WaiveApplication : public juce::JUCEApplication,
                         public EditSession::Listener,
                         public ProjectManager::Listener
{
public:
    const juce::String getApplicationName() override    { return "Waive"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        lookAndFeel = std::make_unique<waive::WaiveLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        engine = std::make_unique<te::Engine> ("Waive");
        engine->getDeviceManager().initialise (2, 2);

        editSession = std::make_unique<EditSession> (*engine);
        jobQueue = std::make_unique<waive::JobQueue>();
        projectManager = std::make_unique<ProjectManager> (*editSession);

        commandHandler = std::make_unique<CommandHandler> (editSession->getEdit());
        undoableHandler = std::make_unique<UndoableCommandHandler> (*commandHandler, *editSession);

        // Application properties for settings persistence
        juce::PropertiesFile::Options propOpts;
        propOpts.applicationName = "Waive";
        propOpts.folderName = "Waive";
        propOpts.filenameSuffix = ".settings";
        propOpts.osxLibrarySubFolder = "Application Support";
        appProperties = std::make_unique<juce::ApplicationProperties>();
        appProperties->setStorageParameters (propOpts);

        // AI system
        aiSettings = std::make_unique<waive::AiSettings>();
        aiSettings->loadFromProperties (*appProperties);

        toolRegistry = std::make_unique<waive::ToolRegistry>();

        // External tool runner
        externalToolRunner = std::make_unique<waive::ExternalToolRunner>();

        // Add built-in tools directory (next to executable)
        auto builtInToolsDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                   .getParentDirectory().getParentDirectory().getChildFile ("tools");
        if (builtInToolsDir.isDirectory())
            externalToolRunner->addToolsDirectory (builtInToolsDir);

        // Add user tools directory
        externalToolRunner->addToolsDirectory (externalToolRunner->getToolsDirectory());

        // Scan and register external tools
        toolRegistry->scanAndRegisterExternalTools (*externalToolRunner);

        aiAgent = std::make_unique<waive::AiAgent> (*aiSettings, *undoableHandler,
                                                     *toolRegistry, *jobQueue);

        editSession->addListener (this);
        projectManager->addListener (this);

        mainWindow = std::make_unique<MainWindow> (getWindowTitle(), *undoableHandler,
                                                   *editSession, *jobQueue, *projectManager,
                                                   aiAgent.get(), aiSettings.get());

        // Wire tool context provider for AI agent
        aiAgent->setToolContextProvider ([this]() -> waive::ToolExecutionContext
        {
            auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
            jassert (mc != nullptr);

            auto cacheDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("Waive").getChildFile ("cache");
            cacheDir.createDirectory();

            return { *editSession, *projectManager, mc->getSessionComponentForTesting(),
                     *mc->getModelManager(), cacheDir };
        });

        // Schedule plugin scan in background after UI shown
        jobQueue->submit ({"ScanPlugins", "System"},
                          [this] (waive::ProgressReporter&)
                          {
                              if (engine)
                                  engine->getPluginManager().initialise();
                          });

        // Restore chat history for the initial (unsaved) project
        if (aiAgent)
            aiAgent->loadConversation (getChatHistoryFile());
    }

    void shutdown() override
    {
        if (aiSettings && appProperties)
            aiSettings->saveToProperties (*appProperties);

        if (aiAgent)
            aiAgent->saveConversation (getChatHistoryFile());

        if (projectManager)
            projectManager->removeListener (this);
        if (editSession)
            editSession->removeListener (this);
        mainWindow.reset();
        aiAgent.reset();
        externalToolRunner.reset();
        toolRegistry.reset();
        aiSettings.reset();
        appProperties.reset();
        undoableHandler.reset();
        commandHandler.reset();
        projectManager.reset();
        jobQueue.reset();
        editSession.reset();
        engine.reset();

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    //==============================================================================
    // EditSession::Listener
    void editAboutToChange() override
    {
        // Transport must stop before swapping edit
        editSession->getEdit().getTransport().stop (false, false);
    }

    void editChanged() override
    {
        // Reconstruct CommandHandler with the new edit reference
        commandHandler = std::make_unique<CommandHandler> (editSession->getEdit());
        undoableHandler->setCommandHandler (*commandHandler);

        updateWindowTitle();

        if (aiAgent)
        {
            aiAgent->clearConversation();
            aiAgent->loadConversation (getChatHistoryFile());
        }
    }

    void editStateChanged() override
    {
        if (projectManager)
            projectManager->notifyDirtyChanged();
    }

    //==============================================================================
    // ProjectManager::Listener
    void projectDirtyChanged() override
    {
        updateWindowTitle();
    }

private:
    juce::File getChatHistoryFile() const
    {
        if (projectManager && projectManager->getCurrentFile().existsAsFile())
        {
            auto projectDir = projectManager->getCurrentFile().getParentDirectory();
            auto chatDir = projectDir.getChildFile (".waive_chat");
            return chatDir.getChildFile (projectManager->getProjectName() + ".chat.json");
        }

        // Unsaved project — use app data directory
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Waive")
                   .getChildFile ("chat_history.json");
    }

    juce::String getWindowTitle() const
    {
        juce::String title = "Waive";
        if (projectManager)
        {
            title += " - " + projectManager->getProjectName();
            if (projectManager->isDirty())
                title += " *";
        }
        return title;
    }

    void updateWindowTitle()
    {
        if (mainWindow)
            mainWindow->setName (getWindowTitle());
    }

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<EditSession> editSession;
    std::unique_ptr<waive::JobQueue> jobQueue;
    std::unique_ptr<ProjectManager> projectManager;

    std::unique_ptr<CommandHandler> commandHandler;
    std::unique_ptr<UndoableCommandHandler> undoableHandler;

    std::unique_ptr<juce::ApplicationProperties> appProperties;
    std::unique_ptr<waive::AiSettings> aiSettings;
    std::unique_ptr<waive::ToolRegistry> toolRegistry;
    std::unique_ptr<waive::ExternalToolRunner> externalToolRunner;
    std::unique_ptr<waive::AiAgent> aiAgent;

    std::unique_ptr<waive::WaiveLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (WaiveApplication)
