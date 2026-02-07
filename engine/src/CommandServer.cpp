#include "CommandServer.h"
#include "CommandHandler.h"
#include <random>
#include <sys/stat.h>

//==============================================================================
// CommandConnection
//==============================================================================

CommandConnection::CommandConnection (CommandHandler& h, const juce::String& authToken)
    : InterprocessConnection (true, 0x0000AD10), handler (h), expectedToken (authToken)
{
    connectionTime = juce::Time::getCurrentTime();
}

void CommandConnection::connectionMade()
{
    juce::Logger::writeToLog ("Client connected. Awaiting authentication...");
}

void CommandConnection::connectionLost()
{
    juce::Logger::writeToLog ("Client disconnected.");
}

void CommandConnection::messageReceived (const juce::MemoryBlock& message)
{
    auto json = message.toString();

    // First message must be authentication token
    if (! authenticated)
    {
        auto elapsed = juce::Time::getCurrentTime() - connectionTime;
        if (elapsed.inSeconds() > 5.0)
        {
            juce::Logger::writeToLog ("Authentication timeout - disconnecting");
            disconnect();
            return;
        }

        auto receivedToken = json.trim();
        if (receivedToken == expectedToken)
        {
            authenticated = true;
            juce::Logger::writeToLog ("Client authenticated successfully");

            // Send acknowledgment
            auto ack = juce::String ("AUTH_OK");
            juce::MemoryBlock ackBlock (ack.toRawUTF8(), ack.getNumBytesAsUTF8());
            sendMessage (ackBlock);
            return;
        }
        else
        {
            juce::Logger::writeToLog ("Invalid authentication token - disconnecting");
            disconnect();
            return;
        }
    }

    juce::Logger::writeToLog ("Received: " + json);

    juce::String response;

    // Marshal Edit mutations onto the message thread. This keeps behavior sane in the GUI app,
    // and also makes the headless server thread-safe with respect to JUCE/Tracktion state.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        response = handler.handleCommand (json);
    }
    else
    {
        juce::WaitableEvent done;

        juce::MessageManager::callAsync ([&]
        {
            response = handler.handleCommand (json);
            done.signal();
        });

        done.wait();
    }

    juce::MemoryBlock responseBlock (response.toRawUTF8(),
                                     response.getNumBytesAsUTF8());
    sendMessage (responseBlock);
}

//==============================================================================
// CommandServer
//==============================================================================

CommandServer::CommandServer (CommandHandler& h, int p)
    : handler (h), port (p)
{
}

CommandServer::~CommandServer()
{
    stop();

    juce::ScopedLock lock (connectionLock);
    connections.clear();

    // Clean up auth token file
    if (authTokenFile.existsAsFile())
        authTokenFile.deleteFile();
}

void CommandServer::generateAuthToken()
{
    // Generate 32-byte random hex token using cryptographically secure source
    std::random_device rd;
    std::mt19937 gen (rd());
    std::uniform_int_distribution<> dis (0, 255);

    juce::String token;
    for (int i = 0; i < 32; ++i)
    {
        int byte = dis (gen);
        token += juce::String::toHexString (byte).paddedLeft ('0', 2);
    }
    authToken = token;

    // Write to temp file with mode 0600
    authTokenFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile (".waive_auth_token_" + juce::String (port));

    authTokenFile.replaceWithText (authToken);

    // Enforce mode 0600 (owner read/write only) on Unix
    #if JUCE_LINUX || JUCE_MAC
    chmod (authTokenFile.getFullPathName().toRawUTF8(), 0600);
    #else
    authTokenFile.setReadOnly (true, false);
    #endif

    juce::Logger::writeToLog ("Authentication token written to: " + authTokenFile.getFullPathName());
}

bool CommandServer::start()
{
    generateAuthToken();
    return beginWaitingForSocket (port, "127.0.0.1");
}

juce::InterprocessConnection* CommandServer::createConnectionObject()
{
    auto* conn = new CommandConnection (handler, authToken);

    juce::ScopedLock lock (connectionLock);
    connections.add (conn);

    return conn;
}
