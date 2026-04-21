#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "CommandServer.h"
#include "CommandHandler.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"

namespace te = tracktion;

//==============================================================================
class WaiveApplication : public juce::JUCEApplicationBase
{
public:
    WaiveApplication() = default;

    const juce::String getApplicationName() override    { return "Waive Engine"; }
    const juce::String getApplicationVersion() override  { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override            { return false; }

    void initialise (const juce::String& commandLine) override
    {
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        juce::Logger::writeToLog ("Waive Engine v0.1.0 starting...");

        // ── Tracktion Engine ────────────────────────────────────────────
        engine = std::make_unique<te::Engine> ("Waive");
        engine->getPluginManager().initialise();

        editSession = std::make_unique<EditSession> (*engine);
        edit = &editSession->getEdit();
        juce::Logger::writeToLog ("Edit created with 1 audio track.");

        // ── Command layer ───────────────────────────────────────────────
        commandHandler = std::make_unique<CommandHandler> (*edit);
        undoableHandler = std::make_unique<UndoableCommandHandler> (*commandHandler, *editSession);
        commandServer  = std::make_unique<CommandServer> (
            [this] (const juce::String& json)
            {
                return undoableHandler->handleCommand (json);
            },
            port);

        if (commandServer->start())
            juce::Logger::writeToLog ("Command server listening on port " + juce::String (port));
        else
            juce::Logger::writeToLog ("ERROR: Failed to start command server on port " + juce::String (port));
    }

    void shutdown() override
    {
        commandServer.reset();
        undoableHandler.reset();
        commandHandler.reset();
        edit = nullptr;
        editSession.reset();
        engine.reset();

        juce::Logger::writeToLog ("Waive Engine shut down.");
    }

    void anotherInstanceStarted (const juce::String&) override {}
    void systemRequestedQuit() override { quit(); }
    void suspended() override {}
    void resumed() override {}
    void unhandledException (const std::exception*, const juce::String&, int) override {}

private:
    static constexpr int port = 9090;

    std::unique_ptr<te::Engine>         engine;
    std::unique_ptr<EditSession>        editSession;
    te::Edit*                           edit = nullptr;
    std::unique_ptr<CommandHandler>     commandHandler;
    std::unique_ptr<UndoableCommandHandler> undoableHandler;
    std::unique_ptr<CommandServer>      commandServer;
};

//==============================================================================
START_JUCE_APPLICATION (WaiveApplication)
