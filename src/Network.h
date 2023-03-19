#pragma once

#include "Log.h"
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

class ConnectionHandle
{
public:
    explicit ConnectionHandle(int fd) : m_fd(fd) {}

    // will not remove this session from connection map, just 'kicks' the socket connection
    void disconnect();

    void send(const std::string& msg);

private:
    int m_fd;
};

struct MessageConsumer
{
    virtual ~MessageConsumer() = default;

    virtual bool isEnabled() = 0;
    virtual void processMessage(const std::string& msg) = 0;
    virtual void setConnection(std::shared_ptr<ConnectionHandle> connection) = 0;
};

class Acceptor
{
    // sendercompid -> session
    std::unordered_map<std::string, std::unique_ptr<MessageConsumer>> m_sessions;
};

class ReadBuffer
{
public:
    std::vector<std::string> process(int fd, const std::string& text);

private:
    std::unordered_map<int, std::string> m_bufferMap;
    CREATE_LOGGER("ReadBuffer");
};

class ReaderThread
{
public:
    ReaderThread() : m_running(true) {}
    
    void process();
    void process(int fd);

    void queue(int fd);

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

    tbb::concurrent_queue<int> m_readyFDs;

    ReadBuffer m_buffer;

    // fd -> acceptor
    std::unordered_map<int, std::shared_ptr<Acceptor>> m_acceptorSockets;
    // fd -> unassigned connection from acceptor socket
    std::unordered_map<int, std::shared_ptr<Acceptor>> m_unknownConnections;
    // fd -> assigned connection
    std::unordered_map<int, std::shared_ptr<ConnectionHandle>> m_connections;

    CREATE_LOGGER("ReaderThread");
};

struct WriteBuffer
{
    WriteBuffer()
    {
        m_queue.reserve(1024);
        m_buffer.reserve(1024);
    }

    std::string m_queue;
    std::string m_buffer;
};

class WriterThread
{
public:
    void process();

    void send(int fd, const std::string& msg);

private:
    std::atomic<bool> m_running;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    
    std::unordered_map<int, WriteBuffer> m_bufferMap;

    CREATE_LOGGER("WriterThread");
};

class Network
{
public:
    Network();

    void start();
    void stop();

private:
    void run();

    void process(int fd);
    ConnectionHandle createHandle(int fd);

    // port -> acceptor
    std::unordered_map<int, std::shared_ptr<Acceptor>> m_acceptors;

    int m_epollFD;

    size_t m_readerThreadCount;
    std::vector<ReaderThread> m_readerThreads;

    size_t m_writerThreadCount;
    std::vector<WriterThread> m_writerThreads;

    std::atomic<bool> m_running;

    CREATE_LOGGER("Network");
};
