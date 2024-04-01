#pragma once

#include <openfix/Log.h>

#include "Config.h"

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

#include <oneapi/tbb/concurrent_queue.h>

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

    void send(const MsgPacket& msg);
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
        , m_callback(std::move(callback)) {}

    virtual ~NetworkHandler() = default;

    void start();
    void stop();

    void processMessage(const std::string& msg);
    void send(const MsgPacket& msg);

    void disconnect();
    void setConnection(std::shared_ptr<ConnectionHandle> connection);
    bool isConnected();

private:
    const SessionSettings& m_settings;
    Network& m_network;
    MessageCallback_T m_callback;

    std::mutex m_mutex;
    std::shared_ptr<ConnectionHandle> m_connection;
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

private:
    std::unordered_map<int, std::string> m_bufferMap;
    char m_buffer[READ_BUF_SIZE];

    CREATE_LOGGER("ReadBuffer");
};

class ReaderThread
{
public:
    ReaderThread(Network& network) : m_running(true), m_network(network) {}
    
    void process();
    void process(int fd);

    void queue(int fd);
    void disconnect(int fd);

    bool addConnection(const std::shared_ptr<NetworkHandler>& handler, int fd);

    void addAcceptor(const std::shared_ptr<NetworkHandler>& handler, const SessionID_T sessionID, int fd);
    void removeAcceptor(const SessionID_T sessionID, int fd);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    bool accept(int serverFD, std::string& address);

    std::atomic<bool> m_running;
    std::mutex m_mutex;
    std::condition_variable m_cv;
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

    std::string m_queue;
    std::string m_buffer;

    tbb::concurrent_queue<SendCallback_T> m_sendCallbacks;
};

class WriterThread
{
public:
    WriterThread(Network& network) : m_running(true), m_network(network) {}

    void process();

    void send(int fd, const MsgPacket& msg);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    std::atomic<bool> m_running;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    
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

    bool addAcceptor(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler);
    bool removeAcceptor(const SessionSettings& settings);

private:
    void run();

    bool addClient(int fd);

    // acceptor port -> fd
    std::mutex m_mutex;
    std::unordered_map<int, int> m_acceptors;

    int m_epollFD;

    size_t m_readerThreadCount;
    std::vector<std::unique_ptr<ReaderThread>> m_readerThreads;

    size_t m_writerThreadCount;
    std::vector<std::unique_ptr<WriterThread>> m_writerThreads;

    std::atomic<bool> m_running;

    CREATE_LOGGER("Network");

    friend class ReaderThread;
    friend class WriterThread;
    friend class ConnectionHandle;
};
