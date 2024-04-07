#pragma once

#include <oneapi/tbb/concurrent_queue.h>
#include <openfix/Log.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Config.h"

#define READ_BUF_SIZE 1024
#define WRITE_BUF_SIZE 1024

class Network;
class ReaderThread;

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
    using MessageCallback_T = std::function<void(const std::string&)>;

    NetworkHandler(const SessionSettings& settings, Network& network, MessageCallback_T callback)
        : m_settings(settings)
        , m_network(network)
        , m_callback(std::move(callback))
        , m_valid(true)
    {}

    virtual ~NetworkHandler() = default;

    void start();
    void stop();

    void processMessage(const std::string& msg);
    void send(MsgPacket&& msg);

    void disconnect();
    void setConnection(std::shared_ptr<ConnectionHandle> connection);
    bool isConnected();

    void invalidate();

private:
    const SessionSettings& m_settings;
    Network& m_network;
    MessageCallback_T m_callback;

    std::recursive_mutex m_mutex;
    std::shared_ptr<ConnectionHandle> m_connection;

    std::atomic<bool> m_valid;

    CREATE_LOGGER("Network");
};

struct Acceptor
{
    // sessionID -> session network handler
    std::unordered_map<SessionID_T, std::shared_ptr<NetworkHandler>> m_sessions;
};

class ReadBuffer
{
public:
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
    std::unordered_map<int, std::string> m_bufferMap;

    CREATE_LOGGER("ReadBuffer");
};

class ReaderThread
{
public:
    ReaderThread(Network& network)
        : m_running(true)
        , m_network(network)
    {
        m_thread = std::thread([&] { process(); });
    }

    void process();
    void process(int fd);

    void queue(int fd);
    void disconnect(int fd);

    bool addConnection(const std::shared_ptr<NetworkHandler>& handler, int fd);
    void accept(int fd, const std::shared_ptr<Acceptor>& acceptor);

    bool hasAcceptor(const SessionID_T sessionID, int fd);
    void addAcceptor(const std::shared_ptr<NetworkHandler>& handler, const SessionID_T sessionID, int fd);
    void removeAcceptor(const SessionID_T sessionID, int fd);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    std::atomic<bool> m_running;
    std::recursive_mutex m_mutex;
    std::condition_variable_any m_cv;
    std::thread m_thread;

    tbb::concurrent_queue<int> m_readyFDs;

    ReadBuffer m_buffer;

    // fd -> acceptor
    std::unordered_map<int, std::shared_ptr<Acceptor>> m_acceptorSockets;
    // fd -> unassigned connection from acceptor socket
    std::unordered_map<int, std::shared_ptr<Acceptor>> m_unknownConnections;
    // fd -> assigned connection
    std::unordered_map<int, std::shared_ptr<NetworkHandler>> m_connections;

    Network& m_network;

    CREATE_LOGGER("ReaderThread");
};

struct WriteBuffer
{
    WriteBuffer()
    {
        m_queue.reserve(WRITE_BUF_SIZE);
        m_buffer.reserve(WRITE_BUF_SIZE);
    }

    struct MsgMetadata
    {
        MsgMetadata(MsgPacket&& packet)
        {
            m_callback = std::move(packet.m_callback);
            m_msg_size = packet.m_msg.size();
        }

        SendCallback_T m_callback;
        size_t m_msg_size;
    };

    std::string m_queue;
    std::string m_buffer;

    std::list<MsgMetadata> m_meta_queue;
    std::list<MsgMetadata> m_meta_buffer;

    std::atomic<bool> m_valid = true;
};

class WriterThread
{
public:
    WriterThread(Network& network)
        : m_running(true)
        , m_network(network)
    {
        m_thread = std::thread([&] { process(); });
    }

    void notify();
    void process();

    void send(int fd, MsgPacket&& msg);
    void disconnect(int fd);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    std::atomic<bool> m_running;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    std::thread m_thread;

    std::shared_mutex m_bufferMutex;
    std::unordered_map<int, WriteBuffer> m_bufferMap;

    Network& m_network;

    CREATE_LOGGER("WriterThread");
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

private:
    void run();

    bool accept(int server_fd, const std::shared_ptr<Acceptor>& acceptor);

    // acceptor port -> fd
    std::mutex m_mutex;
    std::unordered_map<int, int> m_acceptors;

    int m_epollFD;

    size_t m_readerThreadCount;
    std::vector<std::unique_ptr<ReaderThread>> m_readerThreads;

    size_t m_writerThreadCount;
    std::vector<std::unique_ptr<WriterThread>> m_writerThreads;

    std::atomic<bool> m_running;
    std::thread m_thread;

    CREATE_LOGGER("Network");

    friend class ReaderThread;
    friend class WriterThread;
    friend class ConnectionHandle;
};
