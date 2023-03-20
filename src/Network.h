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

#define READ_BUF_SIZE 1024

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

struct Acceptor
{
    // sendercompid -> session
    std::unordered_map<std::string, std::unique_ptr<MessageConsumer>> m_sessions;
};

class ReadBuffer
{
public:
    std::vector<std::string> read(int fd);

private:
    std::unordered_map<int, std::string> m_bufferMap;
    char m_buffer[READ_BUF_SIZE];

    CREATE_LOGGER("ReadBuffer");
};

class ReaderThread
{
public:
    ReaderThread(int epollFD) : m_running(true), m_epollFD(epollFD) {}
    
    void process();
    void process(int fd);

    void queue(int fd);

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

    int m_epollFD;

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
    std::vector<std::unique_ptr<ReaderThread>> m_readerThreads;

    size_t m_writerThreadCount;
    std::vector<std::unique_ptr<WriterThread>> m_writerThreads;

    std::atomic<bool> m_running;

    CREATE_LOGGER("Network");
};
