#pragma once

#include <JuceHeader.h>

class CommandHandler;

//==============================================================================
/** A single client connection to the Waive command server. */
class CommandConnection : public juce::InterprocessConnection
{
public:
    explicit CommandConnection (CommandHandler& handler, const juce::String& authToken);

    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;

private:
    CommandHandler& handler;
    juce::String expectedToken;
    bool authenticated = false;
    juce::Time connectionTime;
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

    /** Get the authentication token (empty if server not started). */
    juce::String getAuthToken() const { return authToken; }

    /** Get the authentication token file path (empty if server not started). */
    juce::File getAuthTokenFile() const { return authTokenFile; }

private:
    CommandHandler& handler;
    int port;
    juce::String authToken;
    juce::File authTokenFile;

    juce::OwnedArray<CommandConnection> connections;
    juce::CriticalSection connectionLock;

    void generateAuthToken();
};
