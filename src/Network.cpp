#include "Network.h"

#include "Message.h"

#include <cerrno>
#include <iostream>

#include <sys/epoll.h>
#include <unistd.h>

#define EVENT_BUF_SIZE 256

Network::Network() : m_epollFD(-1), m_running(false)
{
    m_writerThreadCount = PlatformSettings::getLong(PlatformSettings::WRITER_THREADS);
    m_readerThreadCount = PlatformSettings::getLong(PlatformSettings::READER_THREADS);
}

void Network::start()
{
    m_epollFD = epoll_create1(0);
    if (m_epollFD == -1)
    {
        if (errno == EMFILE)
        {
            throw std::runtime_error("Couldn't initialize epoll, process FD limit reached");
        }
        else if (errno == ENFILE)
        {
            throw std::runtime_error("Couldn't initialize epoll, system FD limit reached");
        }
        else if (errno == ENOMEM)
        {
            throw std::runtime_error("Couldn't initialize epoll, system memory limit reached");
        }

        throw std::runtime_error("Couldn't initialize epoll, unknown error");
    }

    std::cout << m_epollFD << std::endl;

    for (size_t i = 0; i < m_readerThreadCount; ++i)
    {
        m_readerThreads.emplace_back([&]() {
            
        });
    }

    run();
}

void Network::stop()
{
    m_running.store(false, std::memory_order_release);

    // wait for all threads to timeout and terminate
    for (auto& thread : m_readerThreads)
        thread.stop();
    for (auto& thread : m_readerThreads)
        thread.join();
    m_readerThreads.clear();
    
    // finally, close epoll FD
    close(m_epollFD);
}

ConnectionHandle Network::createHandle(int fd)
{
    ConnectionHandle ret{fd};
    return ret;
}

void Network::run()
{
    struct epoll_event events[EVENT_BUF_SIZE];

    int numEvents;
    long timeout = PlatformSettings::getLong(PlatformSettings::EPOLL_TIMEOUT) * 1000;

    while (m_running)
    {
        while ((numEvents = epoll_wait(m_epollFD, events, EVENT_BUF_SIZE, timeout)) > 0)
        {
            for (int i = 0; i < numEvents; i++) {
                int fd = events[i].data.fd;
                m_readerThreads[fd % m_readerThreadCount].queue(fd);
            }
        }
    }
}

////////////////////////////////////////////
//              ReaderThread              //
////////////////////////////////////////////

void ReaderThread::queue(int fd)
{
    // lock as we insert to ensure data change is propogated to reader thread
    // we don't need to lock as we process
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readyFDs.push(fd);
    m_cv.notify_one();
}

void ReaderThread::process()
{
    while (m_running.load(std::memory_order_acquire))
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&](){ return !m_running.load() || !m_readyFDs.empty(); });

        if (!m_running.load(std::memory_order_acquire))
            break;

        int fd;
        if (!m_readyFDs.try_pop(fd))
            continue;

        process(fd);
    }
}

void ReaderThread::process(int fd)
{ 
    // lock here as we're touching connection maps
    std::lock_guard<std::mutex> lock(m_mutex);

    // known connections
    {
        auto it = m_connections.find(fd);
        if (it != m_connections.end())
        {
            return;
        }
    }
    
    // acceptor socket
    {
        auto it = m_acceptorSockets.find(fd);
        if (it != m_acceptorSockets.end())
        {
            return;
        }
    }

    // unknown acceptor connections
    {
        auto it = m_unknownConnections.find(fd);
        if (it != m_unknownConnections.end())
        {

            // verify this message sends a known sessionID and then create new connection
            return;
        }
    }

    LOG_WARN("Received I/O event for unknown fd: " << fd);
}

void ReaderThread::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running.store(false, std::memory_order_release);
    m_cv.notify_one();

    // close open connections
    for (const auto& [k, _] : m_connections)
        close(k);
    // close unknown connections
    for (const auto& [k, _] : m_unknownConnections)
        close(k);
    // close acceptor sockets
    for (const auto& [k, _] : m_acceptorSockets)
        close(k);
}

////////////////////////////////////////////
//               ReadBuffer               //
////////////////////////////////////////////

std::vector<std::string> ReadBuffer::process(int fd, const std::string& text)
{
    std::vector<std::string> ret;

    auto it = m_bufferMap.find(fd);
    if (it == m_bufferMap.end())
        it = m_bufferMap.insert({fd, ""}).first;
    std::string& buffer = it->second;
    buffer.append(text);



    return ret;
}

////////////////////////////////////////////
//              WriterThread              //
////////////////////////////////////////////

void WriterThread::process()
{
    while (m_running.load(std::memory_order_acquire))
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock);

        if (!m_running.load(std::memory_order_acquire))
            return;

        for (auto& [fd, buffer] : m_bufferMap)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!buffer.m_queue.empty())
                    buffer.m_queue.swap(buffer.m_buffer);
            }

            if (buffer.m_buffer.empty())
                continue;

            // send

            buffer.m_buffer.clear();
        }
    }
}

void WriterThread::send(int fd, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bufferMap[fd].m_queue.append(msg);
    m_cv.notify_one();
}
