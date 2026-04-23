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
#include "ModelManager.h"
#include "WaiveLookAndFeel.h"
#include "AiSettings.h"
#include "AiAgent.h"
#include "ProjectChatHistoryController.h"
#include "Tool.h"
#include "ScreenshotCapture.h"

namespace te = tracktion;

namespace
{
juce::File getCompiledSourceToolsDir()
{
#ifdef WAIVE_SOURCE_TOOLS_DIR
    return juce::File (JUCE_STRINGIFY (WAIVE_SOURCE_TOOLS_DIR));
#else
    return {};
#endif
}

juce::Array<juce::File> makeAllowedMediaDirectories (ProjectManager* projectManager,
                                                     const waive::ModelManager* modelManager)
{
    juce::Array<juce::File> directories;
    directories.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    if (projectManager != nullptr)
    {
        auto projectFile = projectManager->getCurrentFile();
        if (projectFile != juce::File())
            directories.addIfNotAlreadyThere (projectFile.getParentDirectory());
    }

    if (modelManager != nullptr)
        directories.addIfNotAlreadyThere (modelManager->getStorageDirectory());

    return directories;
}

MainComponent* getMainComponentFromWindow (juce::DocumentWindow* window)
{
    if (window == nullptr)
        return nullptr;

    return dynamic_cast<MainComponent*> (window->getContentComponent());
}

void quitCurrentWaiveApplication()
{
    if (auto* app = juce::JUCEApplication::getInstance())
        app->quit();
}
}

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::String name, UndoableCommandHandler& handler,
                EditSession& session, waive::JobQueue& queue,
                ProjectManager& projectMgr,
                waive::ToolRegistry& toolRegistry,
                waive::ModelManager& modelManager,
                waive::AiAgent* aiAgent = nullptr,
                waive::AiSettings* aiSettings = nullptr)
        : DocumentWindow (std::move (name),
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (handler, session, queue, projectMgr,
                                            toolRegistry, modelManager,
                                            aiAgent, aiSettings),
                         true);
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

    void systemRequestedQuit() override
    {
        if (projectManager != nullptr && ! projectManager->confirmSaveIfDirty())
            return;

        if (mainWindow != nullptr)
            if (auto* mainComponent = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mainComponent->markCleanShutdown();

        quit();
    }

    void initialise (const juce::String& commandLine) override
    {
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        // Parse command line for screenshot mode
        auto args = juce::StringArray::fromTokens (commandLine, true);
        int ssIdx = args.indexOf ("--screenshot");
        if (ssIdx >= 0 && ssIdx + 1 < args.size())
        {
            screenshotMode = true;
            screenshotOutputDir = juce::File (args[ssIdx + 1]);
            // Remove these args so plugin scan doesn't see them
            args.remove (ssIdx + 1);
            args.remove (ssIdx);
        }

        lookAndFeel = std::make_unique<waive::WaiveLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        engine = std::make_unique<te::Engine> ("Waive");
        engine->getDeviceManager().initialise (2, 2);

        editSession = std::make_unique<EditSession> (*engine);
        jobQueue = std::make_unique<waive::JobQueue>();
        projectManager = std::make_unique<ProjectManager> (*editSession);

        commandHandler = std::make_unique<CommandHandler> (editSession->getEdit());
        commandHandler->setProjectFile (projectManager->getCurrentFile());
        undoableHandler = std::make_unique<UndoableCommandHandler> (*commandHandler, *editSession,
                                                                    projectManager.get());

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

        modelManager = std::make_unique<waive::ModelManager>();
        toolRegistry = std::make_unique<waive::ToolRegistry>();
        commandHandler->setAllowedMediaDirectories (makeAllowedMediaDirectories (projectManager.get(),
                                                                                modelManager.get()));

        // External tool runner
        externalToolRunner = std::make_unique<waive::ExternalToolRunner>();

        // Add built-in tools directory (next to executable)
        auto builtInToolsDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                   .getParentDirectory().getParentDirectory().getChildFile ("tools");
        if (builtInToolsDir.isDirectory())
            externalToolRunner->addToolsDirectory (builtInToolsDir);

        auto sourceToolsDir = getCompiledSourceToolsDir();
        if (sourceToolsDir.isDirectory())
            externalToolRunner->addToolsDirectory (sourceToolsDir);

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
                                                   *toolRegistry, *modelManager,
                                                   aiAgent.get(), aiSettings.get());

        // Wire tool context provider for AI agent
        aiAgent->setToolContextProvider ([this]() -> waive::ToolExecutionContext
        {
            auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
            jassert (mc != nullptr);

            juce::File cacheDir;
            if (projectManager != nullptr && projectManager->getCurrentFile().existsAsFile())
            {
                cacheDir = projectManager->getCurrentFile()
                               .getParentDirectory()
                               .getChildFile (".waive_cache")
                               .getChildFile (projectManager->getProjectName());
            }
            else
            {
                cacheDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("Waive")
                               .getChildFile ("cache")
                               .getChildFile ("unsaved");
            }
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

        if (aiAgent && projectManager)
        {
            projectChatHistory = std::make_unique<waive::ProjectChatHistoryController> (*aiAgent,
                                                                                        *projectManager);
            projectChatHistory->loadCurrentConversation();
        }

        // Screenshot mode: defer capture then exit
        if (screenshotMode)
        {
            auto safeWindow = juce::Component::SafePointer<MainWindow> (mainWindow.get());
            auto outputDir = screenshotOutputDir;

            juce::Timer::callAfterDelay (1500, [safeWindow, outputDir]()
            {
                auto* mainComponent = getMainComponentFromWindow (safeWindow.getComponent());
                if (mainComponent == nullptr)
                    return;

                if (auto* app = dynamic_cast<WaiveApplication*> (juce::JUCEApplication::getInstance()))
                    app->seedDemoContent();

                juce::Component::SafePointer<MainComponent> safeMainComponent (mainComponent);
                juce::Timer::callAfterDelay (500, [safeMainComponent, outputDir]()
                {
                    if (safeMainComponent == nullptr)
                        return;

                    waive::ScreenshotCapture::Options opts;
                    opts.outputDir = outputDir;
                    opts.maxWidth = 1400;
                    opts.jpegQuality = 0.70f;

                    int captured = waive::ScreenshotCapture::captureAll (*safeMainComponent, opts);
                    DBG ("Screenshot mode: captured " + juce::String (captured) + " images to "
                         + outputDir.getFullPathName());

                    safeMainComponent->markCleanShutdown();
                    quitCurrentWaiveApplication();
                });
            });
        }
    }

    void shutdown() override
    {
        if (aiSettings && appProperties)
            aiSettings->saveToProperties (*appProperties);

        if (projectChatHistory)
            projectChatHistory->saveCurrentConversation();

        if (projectManager)
            projectManager->removeListener (this);
        if (editSession)
            editSession->removeListener (this);
        projectChatHistory.reset();
        mainWindow.reset();
        aiAgent.reset();
        externalToolRunner.reset();
        toolRegistry.reset();
        modelManager.reset();
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
        commandHandler->setProjectFile (projectManager != nullptr ? projectManager->getCurrentFile()
                                                                  : juce::File());
        commandHandler->setAllowedMediaDirectories (makeAllowedMediaDirectories (projectManager.get(),
                                                                                modelManager.get()));
        undoableHandler->setCommandHandler (*commandHandler);

        updateWindowTitle();
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

    void projectFileChanged (const juce::File&,
                             const juce::File& projectFile,
                             ProjectManager::FileChangeKind) override
    {
        if (commandHandler != nullptr)
        {
            commandHandler->setProjectFile (projectFile);
            commandHandler->setAllowedMediaDirectories (makeAllowedMediaDirectories (projectManager.get(),
                                                                                    modelManager.get()));
        }

        updateWindowTitle();
    }

private:
    void seedDemoContent()
    {
        if (! editSession) return;
        auto& edit = editSession->getEdit();

        // Ensure at least 4 tracks exist
        int numTracks = static_cast<int> (te::getAudioTracks (edit).size());
        if (numTracks < 4)
            edit.ensureNumberOfAudioTracks (4);

        // Name them so the mixer looks populated
        auto tracks = te::getAudioTracks (edit);
        juce::StringArray names = { "Drums", "Bass", "Guitar", "Vocals" };
        for (int i = 0; i < juce::jmin (tracks.size(), names.size()); ++i)
            tracks[i]->setName (names[i]);

        // Set a reasonable tempo
        edit.tempoSequence.getTempo (0)->setBpm (120.0);
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
    std::unique_ptr<waive::ModelManager> modelManager;
    std::unique_ptr<waive::ToolRegistry> toolRegistry;
    std::unique_ptr<waive::ExternalToolRunner> externalToolRunner;
    std::unique_ptr<waive::AiAgent> aiAgent;
    std::unique_ptr<waive::ProjectChatHistoryController> projectChatHistory;

    std::unique_ptr<waive::WaiveLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;

    bool screenshotMode = false;
    juce::File screenshotOutputDir;
};

START_JUCE_APPLICATION (WaiveApplication)
