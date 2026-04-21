#pragma once

#include <JuceHeader.h>
#include <functional>

//==============================================================================
/** A single client connection to the Waive command server. */
class CommandConnection : public juce::InterprocessConnection
{
public:
    using CommandCallback = std::function<juce::String (const juce::String&)>;

    explicit CommandConnection (CommandCallback callback, const juce::String& authToken);

    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;

private:
    CommandCallback commandCallback;
    juce::String expectedToken;
    bool authenticated = false;
    juce::Time connectionTime;
};

//==============================================================================
/** TCP server that accepts connections and dispatches JSON commands. */
class CommandServer : public juce::InterprocessConnectionServer
{
public:
    using CommandCallback = CommandConnection::CommandCallback;

    CommandServer (CommandCallback callback, int port);
    ~CommandServer() override;

    bool start();

    juce::InterprocessConnection* createConnectionObject() override;

    /** Get the authentication token (empty if server not started). */
    juce::String getAuthToken() const { return authToken; }

    /** Get the authentication token file path (empty if server not started). */
    juce::File getAuthTokenFile() const { return authTokenFile; }

private:
    CommandCallback commandCallback;
    int port;
    juce::String authToken;
    juce::File authTokenFile;

    juce::OwnedArray<CommandConnection> connections;
    juce::CriticalSection connectionLock;

    void generateAuthToken();
};
