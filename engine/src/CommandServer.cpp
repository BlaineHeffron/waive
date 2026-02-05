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

    auto response = handler.handleCommand (json);

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
