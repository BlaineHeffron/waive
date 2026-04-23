#include "CommandServer.h"

#if JUCE_WINDOWS
#include <bcrypt.h>
#endif

#if JUCE_WINDOWS
#pragma comment(lib, "bcrypt")
#endif

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#if ! JUCE_WINDOWS
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
void closeConnectionTransport (juce::InterprocessConnection& connection)
{
    if (auto* socket = connection.getSocket())
        socket->close();

    if (auto* pipe = connection.getPipe())
        pipe->close();
}
}

//==============================================================================
// CommandConnection
//==============================================================================

CommandConnection::CommandConnection (CommandCallback callback, const juce::String& authToken,
                                      int timeoutMs)
    : InterprocessConnection (false, 0x0000AD10),
      commandCallback (std::move (callback)),
      expectedToken (authToken),
      authTimeoutMs (timeoutMs)
{
}

CommandConnection::~CommandConnection()
{
    stopAuthTimeoutThread();
    disconnect (1000, juce::InterprocessConnection::Notify::no);
}

void CommandConnection::connectionMade()
{
    connectionTime = juce::Time::getCurrentTime();
    if (authTimeoutMs > 0)
        startAuthTimeoutThread();

    juce::Logger::writeToLog ("Client connected. Awaiting authentication...");
}

void CommandConnection::connectionLost()
{
    stopAuthTimeoutThread();
    juce::Logger::writeToLog ("Client disconnected.");
}

void CommandConnection::messageReceived (const juce::MemoryBlock& message)
{
    auto json = message.toString();

    // First message must be authentication token
    if (! authenticated)
    {
        const auto elapsedMs = (juce::Time::getCurrentTime() - connectionTime).inMilliseconds();
        if (authTimeoutMs > 0 && elapsedMs > authTimeoutMs)
        {
            juce::Logger::writeToLog ("Authentication timeout - disconnecting");
            closeConnectionTransport (*this);
            return;
        }

        auto receivedToken = json.trim();
        if (receivedToken == expectedToken)
        {
            authenticated = true;
            stopAuthTimeout.store (true);
            stopAuthTimeoutThread();
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
            closeConnectionTransport (*this);
            return;
        }
    }

    juce::Logger::writeToLog ("Received: " + json);

    juce::String response;

    // Marshal Edit mutations onto the message thread. This keeps behavior sane in the GUI app,
    // and also makes the headless server thread-safe with respect to JUCE/Tracktion state.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        response = commandCallback != nullptr
                     ? commandCallback (json)
                     : "{\"status\":\"error\",\"message\":\"No command handler configured\"}";
    }
    else
    {
        juce::MessageManagerLock messageManagerLock;
        if (! messageManagerLock.lockWasGained())
            response = "{\"status\":\"error\",\"message\":\"Failed to acquire message-thread lock\"}";
        else
            response = commandCallback != nullptr
                         ? commandCallback (json)
                         : "{\"status\":\"error\",\"message\":\"No command handler configured\"}";
    }

    juce::MemoryBlock responseBlock (response.toRawUTF8(),
                                     response.getNumBytesAsUTF8());
    sendMessage (responseBlock);
}

void CommandConnection::startAuthTimeoutThread()
{
    stopAuthTimeoutThread();
    stopAuthTimeout.store (false);

    authTimeoutThread = std::thread ([this]
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + (double) authTimeoutMs;

        while (! stopAuthTimeout.load() && ! authenticated.load())
        {
            if (juce::Time::getMillisecondCounterHiRes() >= deadline)
            {
                const auto elapsedMs = (juce::Time::getCurrentTime() - connectionTime).inMilliseconds();
                juce::Logger::writeToLog ("Authentication timeout after " + juce::String (elapsedMs)
                                          + " ms - disconnecting");
                closeConnectionTransport (*this);
                return;
            }

            juce::Thread::sleep (10);
        }
    });
}

void CommandConnection::stopAuthTimeoutThread()
{
    stopAuthTimeout.store (true);

    if (! authTimeoutThread.joinable())
        return;

    if (authTimeoutThread.get_id() == std::this_thread::get_id())
    {
        authTimeoutThread.detach();
        return;
    }

    authTimeoutThread.join();
}

//==============================================================================
// CommandServer
//==============================================================================

namespace
{
bool fillSecureRandomBytes (std::array<unsigned char, 32>& bytes)
{
#if JUCE_WINDOWS
    return BCryptGenRandom (nullptr, bytes.data(), (ULONG) bytes.size(),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) == STATUS_SUCCESS;
#else
    std::ifstream randomStream ("/dev/urandom", std::ios::binary);
    if (! randomStream.good())
        return false;

    randomStream.read (reinterpret_cast<char*> (bytes.data()), (std::streamsize) bytes.size());
    return randomStream.good();
#endif
}

juce::String bytesToHexString (const std::array<unsigned char, 32>& bytes)
{
    juce::String token;
    for (const auto byte : bytes)
        token += juce::String::toHexString ((int) byte).paddedLeft ('0', 2);
    return token;
}

juce::File getAuthTokenFilePathForPort (int port)
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("Waive");
    return directory.getChildFile (".waive_auth_token_" + juce::String (port));
}

bool writeAuthTokenFile (const juce::File& authTokenFile, const juce::String& token)
{
    auto parentDir = authTokenFile.getParentDirectory();
    if (parentDir == juce::File() || (! parentDir.exists() && parentDir.createDirectory().failed()))
        return false;

#if JUCE_WINDOWS
    juce::FileOutputStream outputStream (authTokenFile);
    if (! outputStream.openedOk())
        return false;

    if (! outputStream.writeText (token, false, false, nullptr))
        return false;

    return outputStream.flush().wasOk();
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
   #if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
   #endif

    const auto path = authTokenFile.getFullPathName().toRawUTF8();
    const auto fd = ::open (path, flags, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return false;

    const auto closeFd = [&fd]
    {
        if (fd >= 0)
            ::close (fd);
    };

    const auto tokenBytes = token.toRawUTF8();
    const auto bytesToWrite = (size_t) std::strlen (tokenBytes);
    ssize_t bytesWritten = 0;

    while ((size_t) bytesWritten < bytesToWrite)
    {
        const auto result = ::write (fd, tokenBytes + bytesWritten, bytesToWrite - (size_t) bytesWritten);
        if (result < 0)
        {
            const auto lastErrno = errno;
            closeFd();
            errno = lastErrno;
            (void) authTokenFile.deleteFile();
            return false;
        }

        bytesWritten += result;
    }

    if (::fchmod (fd, S_IRUSR | S_IWUSR) != 0)
    {
        closeFd();
        (void) authTokenFile.deleteFile();
        return false;
    }

    closeFd();
    return true;
#endif
}
}

CommandServer::CommandServer (CommandCallback callback, int p, int timeoutMs)
    : commandCallback (std::move (callback)), port (p), authTimeoutMs (timeoutMs)
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
    std::array<unsigned char, 32> randomBytes {};
    if (! fillSecureRandomBytes (randomBytes))
    {
        authToken = {};
        juce::Logger::writeToLog ("CommandServer: failed to obtain cryptographic random bytes for auth token");
        return;
    }

    authToken = bytesToHexString (randomBytes);
}

bool CommandServer::start()
{
    generateAuthToken();
    if (authToken.isEmpty())
        return false;

    if (! beginWaitingForSocket (port, "127.0.0.1"))
    {
        authToken = {};
        return false;
    }

    auto nextAuthTokenFile = getAuthTokenFilePathForPort (port);
    if (! writeAuthTokenFile (nextAuthTokenFile, authToken))
    {
        stop();
        authToken = {};
        juce::Logger::writeToLog ("CommandServer: failed to write auth token file");
        return false;
    }

    authTokenFile = nextAuthTokenFile;
    juce::Logger::writeToLog ("Authentication token written to: " + authTokenFile.getFullPathName());
    return true;
}

juce::InterprocessConnection* CommandServer::createConnectionObject()
{
    auto* conn = new CommandConnection (commandCallback, authToken, authTimeoutMs);

    juce::ScopedLock lock (connectionLock);
    connections.add (conn);

    return conn;
}
