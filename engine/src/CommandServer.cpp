#include "CommandServer.h"
#include "CommandHandler.h"

//==============================================================================
// CommandConnection
//==============================================================================

CommandConnection::CommandConnection (CommandHandler& h)
    : InterprocessConnection (true, 0x0000AD10), handler (h)
{
}

void CommandConnection::connectionMade()
{
    juce::Logger::writeToLog ("Client connected.");
}

void CommandConnection::connectionLost()
{
    juce::Logger::writeToLog ("Client disconnected.");
}

void CommandConnection::messageReceived (const juce::MemoryBlock& message)
{
    auto json = message.toString();
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
}

bool CommandServer::start()
{
    return beginWaitingForSocket (port, "127.0.0.1");
}

juce::InterprocessConnection* CommandServer::createConnectionObject()
{
    auto* conn = new CommandConnection (handler);

    juce::ScopedLock lock (connectionLock);
    connections.add (conn);

    return conn;
}
