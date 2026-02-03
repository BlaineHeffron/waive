#pragma once

#include <JuceHeader.h>

class CommandHandler;

//==============================================================================
/** A single client connection to the Waive command server. */
class CommandConnection : public juce::InterprocessConnection
{
public:
    explicit CommandConnection (CommandHandler& handler);

    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;

private:
    CommandHandler& handler;
};

//==============================================================================
/** TCP server that accepts connections and dispatches JSON commands. */
class CommandServer : public juce::InterprocessConnectionServer
{
public:
    CommandServer (CommandHandler& handler, int port);
    ~CommandServer() override;

    bool start();

    juce::InterprocessConnection* createConnectionObject() override;

private:
    CommandHandler& handler;
    int port;

    juce::OwnedArray<CommandConnection> connections;
    juce::CriticalSection connectionLock;
};
