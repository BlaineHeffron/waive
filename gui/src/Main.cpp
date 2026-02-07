#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "MainComponent.h"
#include "CommandHandler.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "ProjectManager.h"
#include "JobQueue.h"
#include "WaiveLookAndFeel.h"

namespace te = tracktion;

//==============================================================================
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::String name, UndoableCommandHandler& handler,
                EditSession& session, waive::JobQueue& queue,
                ProjectManager& projectMgr)
        : DocumentWindow (std::move (name),
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (handler, session, queue, projectMgr), true);
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

        editSession->addListener (this);
        projectManager->addListener (this);

        mainWindow = std::make_unique<MainWindow> (getWindowTitle(), *undoableHandler,
                                                   *editSession, *jobQueue, *projectManager);

        // Schedule plugin scan in background after UI shown
        jobQueue->submit ({"ScanPlugins", "System"},
                          [this] (waive::ProgressReporter&)
                          {
                              if (engine)
                                  engine->getPluginManager().initialise();
                          });
    }

    void shutdown() override
    {
        if (projectManager)
            projectManager->removeListener (this);
        if (editSession)
            editSession->removeListener (this);
        mainWindow.reset();
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
    juce::String getWindowTitle() const
    {
        juce::String title = "Waive";
        if (projectManager)
        {
            title += " \u2014 " + projectManager->getProjectName();
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

    std::unique_ptr<waive::WaiveLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (WaiveApplication)
