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

class ConnectionHandle
{
public:
    using DisconnectFunction = std::function<void()>;
    using SendFunction = std::function<void(const std::string&)>;

    ConnectionHandle(int fd, DisconnectFunction disconnect, SendFunction send) 
        : m_fd(fd)
        , m_disconnect(std::move(disconnect))
        , m_send(std::move(send))
    {}

    // will not remove this session from connection map, just 'kicks' the socket connection
    void disconnect()
    {
        m_disconnect();
    }

    void send(const std::string& msg)
    {
        m_send(msg);
    }

    // extremely dangerous, I might remove this
    size_t getFD()
    {
        return m_fd;
    }

private:
    int m_fd;

    DisconnectFunction m_disconnect;
    SendFunction m_send;
};

struct MessageConsumer
{
    virtual ~MessageConsumer() = default;

    virtual bool isEnabled() = 0;
    virtual void processMessage(const std::string& msg) = 0;
    virtual void setConnection(std::shared_ptr<ConnectionHandle> connection) = 0;
};

struct Acceptor
{
    // sendercompid -> session
    std::unordered_map<std::string, std::shared_ptr<MessageConsumer>> m_sessions;
};

class Network;

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

    void addConnection(const MessageConsumer& consumer, int fd);
    void addAcceptor(const SessionSettings& settings, int fd);
    void removeAcceptor(const SessionSettings& settings);

    void stop();

    void join()
    {
        m_thread.join();
    }

private:
    bool accept(int serverFD, std::string& address);
    std::shared_ptr<ConnectionHandle> createHandle(int fd);

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
    std::unordered_map<int, std::shared_ptr<MessageConsumer>> m_connections;

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
};

class WriterThread
{
public:
    WriterThread(Network& network) : m_running(true), m_network(network) {}

    void process();

    void send(int fd, const std::string& msg);

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

    bool connect(const MessageConsumer& consumer, const std::string& hostname, int port);

    bool addAcceptor(const SessionSettings& settings);
    void removeAcceptor(const SessionSettings& settings);

private:
    void run();

    bool addClient(int fd);

    // acceptor port -> fd
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
};
