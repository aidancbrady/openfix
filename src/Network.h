#pragma once

#include <openfix/Log.h>
#include <openfix/Types.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>

#include "Config.h"

#define READ_BUF_SIZE 8192
#define MAX_WRITE_IOVECS 64

class Network;
class ReaderThread;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

using SendCallback_T = std::function<void()>;
struct MsgPacket
{
    std::string m_msg;
    SendCallback_T m_callback;
};

class ConnectionHandle
{
public:
    ConnectionHandle(Network& network, ReaderThread& readerThread, int fd)
        : m_fd(fd)
        , m_network(network)
        , m_readerThread(readerThread)
    {}

    void send(MsgPacket&& msg);
    void disconnect();
    bool isReady() const;

    size_t getFD()
    {
        return m_fd;
    }

private:
    int m_fd;

    Network& m_network;
    ReaderThread& m_readerThread;
};

class NetworkHandler : public std::enable_shared_from_this<NetworkHandler>
{
public:
    using MessageCallback_T = std::function<void(std::string)>;

    NetworkHandler(const SessionSettings& settings, Network& network, MessageCallback_T callback)
        : m_settings(settings)
        , m_network(network)
        , m_callback(std::move(callback))
        , m_valid(true)
        , m_stopped(false)
    {}

    virtual ~NetworkHandler() = default;

    void start();
    void stop();

    void setSocketSettings(int fd);

    void processMessage(std::string msg);
    void send(MsgPacket&& msg);

    void disconnect();
    void setConnection(std::shared_ptr<ConnectionHandle> connection);
    bool isConnected();
    const SessionSettings& getSettings() const
    {
        return m_settings;
    }

    void invalidate();

private:
    const SessionSettings& m_settings;
    Network& m_network;
    MessageCallback_T m_callback;

    std::recursive_mutex m_mutex;
    std::shared_ptr<ConnectionHandle> m_connection;

    std::atomic<bool> m_valid;
    std::atomic<bool> m_stopped;

    CREATE_LOGGER("Network");
};

struct Acceptor
{
    // sessionID -> session network handler
    HashMapT<SessionID_T, std::shared_ptr<NetworkHandler>> m_sessions;
};

class ReadBuffer
{
public:
    ReadBuffer(Network& network)
        : m_network(network)
    {}

    std::vector<std::string> read(int fd);

    void clear(int fd)
    {
        m_bufferMap.erase(fd);
    }

    void clear()
    {
        m_bufferMap.clear();
    }

private:
    HashMapT<int, std::string> m_bufferMap;
    Network& m_network;

    CREATE_LOGGER("ReadBuffer");
};

struct WriteEntry
{
    std::string m_msg;
    SendCallback_T m_callback;
};

struct WriteBuffer
{
    WriteBuffer() = default;

    WriteBuffer(WriteBuffer&& other) noexcept
        : m_queue(std::move(other.m_queue))
        , m_drain(std::move(other.m_drain))
        , m_offset(other.m_offset)
        , m_valid(other.m_valid.load(std::memory_order_relaxed))
    {}

    WriteBuffer& operator=(WriteBuffer&& other) noexcept
    {
        m_queue = std::move(other.m_queue);
        m_drain = std::move(other.m_drain);
        m_offset = other.m_offset;
        m_valid.store(other.m_valid.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }

    // Queue: populated by external threads (under ReaderThread::m_writeMutex)
    std::deque<WriteEntry> m_queue;

    // Drain buffer: swapped from queue, only touched by reader thread
    std::deque<WriteEntry> m_drain;
    size_t m_offset = 0;  // byte offset into first entry for partial sends

    std::atomic<bool> m_valid{true};
};

class ReaderThread
{
public:
    ReaderThread(Network& network);
    ~ReaderThread();

    void process();
    void processRead(int fd);

    void registerFD(int fd);

    // Queue a write from an external thread; wakes reader via eventfd
    void queueWrite(int fd, MsgPacket&& msg);

    // Try non-blocking inline send from caller's thread.
    // Returns true if the full message was sent.
    bool trySend(int fd, MsgPacket& msg);

    void disconnect(int fd);

    bool addConnection(const std::shared_ptr<NetworkHandler>& handler, int fd);
    void accept(int fd, const std::shared_ptr<Acceptor>& acceptor);

    bool hasAcceptor(const SessionID_T sessionID, int fd);
    void addAcceptor(const std::shared_ptr<NetworkHandler>& handler, const SessionID_T sessionID, int fd);
    bool removeAcceptor(const SessionID_T sessionID, int fd);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    void flushWrites();
    void flushWrite(int fd, WriteBuffer& wb);

    std::atomic<bool> m_running;
    std::recursive_mutex m_mutex;
    std::thread m_thread;

    int m_epollFD;
    int m_eventFD;

    // Write buffers: fd -> buffer (m_writeMutex protects m_queue insertion)
    std::mutex m_writeMutex;
    HashMapT<int, WriteBuffer> m_writeBuffers;

    ReadBuffer m_buffer;

    // fd -> acceptor
    HashMapT<int, std::shared_ptr<Acceptor>> m_acceptorSockets;
    // fd -> unassigned connection from acceptor socket
    HashMapT<int, std::shared_ptr<Acceptor>> m_unknownConnections;
    // fd -> assigned connection
    HashMapT<int, std::shared_ptr<NetworkHandler>> m_connections;

    Network& m_network;

    CREATE_LOGGER("ReaderThread");
};

class Network
{
public:
    Network();

    void start();
    void stop();

    bool connect(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler);

    bool hasAcceptor(const SessionSettings& settings);
    bool addAcceptor(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler);
    bool removeAcceptor(const SessionSettings& settings);

    bool progressConnection(int fd);
    bool isConnectionReady(int fd) const;
    bool requiresConnectionProgress(int fd) const;
    bool hasTLS(int fd) const;
    ssize_t readConnection(int fd, void* buf, size_t len);
    ssize_t writeConnection(int fd, const void* buf, size_t len);
    void removeConnection(int fd);

private:
    struct TLSConnection
    {
        std::shared_ptr<SSL_CTX> m_ctx;
        SSL* m_ssl = nullptr;
        bool m_serverMode = false;
        std::atomic<bool> m_ready = false;
        std::mutex m_mutex;
    };

    bool accept(int server_fd, const std::shared_ptr<Acceptor>& acceptor);
    bool createTLSConnection(int fd, const SessionSettings& settings, bool serverMode, const std::string& serverName);
    std::shared_ptr<SSL_CTX> getTLSContext(const SessionSettings& settings, bool serverMode);
    std::shared_ptr<SSL_CTX> createTLSContext(const SessionSettings& settings, bool serverMode) const;
    std::string getTLSContextKey(const SessionSettings& settings, bool serverMode) const;

    // acceptor port -> fd
    std::mutex m_mutex;
    HashMapT<int, int> m_acceptors;
    mutable std::shared_mutex m_tlsMutex;
    HashMapT<int, std::shared_ptr<TLSConnection>> m_tlsConnections;
    std::mutex m_tlsContextsMutex;
    HashMapT<std::string, std::shared_ptr<SSL_CTX>> m_tlsContexts;
    std::atomic<bool> m_hasTLSConnections = false;

    size_t m_readerThreadCount;
    std::vector<std::unique_ptr<ReaderThread>> m_readerThreads;

    std::atomic<bool> m_running;

    CREATE_LOGGER("Network");

    friend class ReaderThread;
    friend class ConnectionHandle;
};
