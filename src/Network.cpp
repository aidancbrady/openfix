#include "Network.h"

#include "Message.h"
#include "Fields.h"

#include <cerrno>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <arpa/inet.h>

#include <unistd.h>
#include <netdb.h>
#include <string.h>

#define EVENT_BUF_SIZE 256
#define ACCEPTOR_BACKLOG 16

const std::string BEGIN_STRING_TAG = std::to_string(FIELD::BeginString) + std::to_string(TAG_ASSIGNMENT_CHAR);
const std::string BODY_LENGTH_TAG = std::to_string(INTERNAL_SOH_CHAR) + std::to_string(FIELD::BodyLength) + std::to_string(TAG_ASSIGNMENT_CHAR);
const std::string SENDER_COMP_ID_TAG = std::to_string(INTERNAL_SOH_CHAR) + std::to_string(FIELD::SenderCompID) + std::to_string(TAG_ASSIGNMENT_CHAR);
const std::string SENDER_SUB_ID_TAG = std::to_string(INTERNAL_SOH_CHAR) + std::to_string(FIELD::SenderSubID) + std::to_string(TAG_ASSIGNMENT_CHAR);
const std::string CHECKSUM_TAG = std::to_string(INTERNAL_SOH_CHAR) + std::to_string(FIELD::CheckSum) + std::to_string(TAG_ASSIGNMENT_CHAR);

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
        throw std::runtime_error("Couldn't initialize epoll: " + std::string(strerror(errno)));
    }

    for (size_t i = 0; i < m_readerThreadCount; ++i)
        m_readerThreads.push_back(std::make_unique<ReaderThread>(*this));

    for (size_t i = 0; i < m_writerThreadCount; ++i)
        m_writerThreads.push_back(std::make_unique<WriterThread>(*this));

    run();
}

void Network::stop()
{
    m_running.store(false, std::memory_order_release);

    // wait for all threads to timeout and terminate
    for (auto& thread : m_readerThreads)
        thread->stop();
    for (auto& thread : m_writerThreads)
        thread->stop();

    for (auto& thread : m_readerThreads)
        thread->join();
    for (auto& thread : m_writerThreads)
        thread->join();

    m_readerThreads.clear();
    m_writerThreads.clear();
    
    // finally, close epoll FD
    close(m_epollFD);
}

bool Network::connect(const std::string& hostname, int port)
{
    int fd = 0;

    struct addrinfo hints;
    struct addrinfo* ret;

    hints.ai_family = AF_UNSPEC; // allow ipv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int err = getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &ret);
    if (err != 0)
    {
        LOG_ERROR("Error in hostname resolution: " << gai_strerror(err));
        return false;
    }

    for (struct addrinfo* addr = ret; addr != nullptr; addr = addr->ai_next)
    {
        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd == -1)
        {
            err = errno;
            continue;
        }

        if (::connect(fd, addr->ai_addr, addr->ai_addrlen) < 0)
            break;

        err = errno;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(ret);

    if (!addClient(fd))
    {
        close(fd);
        return false;
    }

    if (!m_readerThreads[fd % m_readerThreadCount].addClient(fd))
    {
        close(fd);
        return false;
    }
}

bool Network::addAcceptor(const SessionSettings& settings)
{
    int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    int fd = 0;

    if ((fd = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        LOG_ERROR("Unable to create socket: " << strerror(errno));
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_UNSPEC; // support ipv4
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (const struct sockaddr *) &addr, sizeof(addr)) < 0) 
    {
        LOG_ERROR("Couldn't bind to port: " << strerror(errno));
        close(fd);
        return false;
    }

    if (listen(fd, ACCEPTOR_BACKLOG) < 0) 
    {
        LOG_ERROR("Couldn't listen to accept port: " << strerror(errno));
        close(fd);
        return false;
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = fd;

    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0) 
    {
        LOG_ERROR("Couldn't add acceptor socket to epoll wait list: " << strerror(errno));
        close(fd);
        return false;
    }

    m_readerThreads[fd % m_readerThreadCount]->addAcceptor(settings, fd);
}

void Network::removeAcceptor(const SessionSettings& settings)
{

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
            for (int i = 0; i < numEvents; i++) 
            {
                int fd = events[i].data.fd;

                if (events[i].events & EPOLLIN)
                {
                    // data received
                    m_readerThreads[fd % m_readerThreadCount]->queue(fd);
                }
                else if (events[i].events & EPOLLRDHUP)
                {
                    // connection hangup
                    m_readerThreads[fd % m_readerThreadCount]->disconnect(fd);
                }
            }
        }
    }
}

bool Network::addClient(int fd)
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    event.data.fd = fd;

    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        LOG_WARN("Failed to register connection with epoll");
        return false;
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

void ReaderThread::disconnect(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear(fd);
    auto it = m_connections.find(fd);
    if (m_connections.find(fd) != m_connections.end())
    {
        it->second->setConnection(nullptr);
        m_connections.erase(fd);
        return;
    }
    
    if (m_unknownConnections.find(fd) != m_unknownConnections.end())
    {
        m_unknownConnections.erase(fd);
        return;
    }
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
            auto msgs = m_buffer.read(fd);
            for (const auto& msg : msgs)
                it->second->processMessage(msg);
            return;
        }
    }
    
    // acceptor socket
    {
        auto it = m_acceptorSockets.find(fd);
        if (it != m_acceptorSockets.end())
        {
            std::string address;
            if (accept(fd, address))
            {
                m_unknownConnections[fd] = it->second;
                LOG_INFO("Accepted new connection: " << address);
            }

            return;
        }
    }

    // unknown acceptor connections
    {
        auto it = m_unknownConnections.find(fd);
        if (it != m_unknownConnections.end())
        {
            auto msgs = m_buffer.read(fd);
            if (!msgs.empty())
            {
                // this connection is either invalid or will be known
                m_unknownConnections.erase(fd);

                const auto& msg = msgs[0];
                size_t ptr = msg.find(SENDER_COMP_ID_TAG);
                if (ptr == std::string::npos)
                {
                    LOG_ERROR("Received message without SenderCompID");
                    close(fd);
                    return;
                }

                size_t end = msg.find(INTERNAL_SOH_CHAR, ptr);
                auto cpty = msg.substr(ptr + SENDER_COMP_ID_TAG.size(), end - ptr + SENDER_COMP_ID_TAG.size());

                ptr = msg.find(SENDER_SUB_ID_TAG);
                if (ptr != std::string::npos)
                {
                    end = msg.find(INTERNAL_SOH_CHAR, ptr);
                    cpty.append(":" + msg.substr(ptr + SENDER_SUB_ID_TAG.size(), end - ptr + SENDER_SUB_ID_TAG.size()));
                }
                else
                {
                    cpty.append(":*");
                }

                auto consumerIt = it->second->m_sessions.find(cpty);
                if (consumerIt == it->second->m_sessions.end())
                {
                    LOG_ERROR("Received connection from unknown counterparty: " << cpty);
                    close(fd);
                    return;
                }

                // create new connection
                consumerIt->second->setConnection(createHandle(fd));
                m_connections[fd] = consumerIt->second;
            }

            return;
        }
    }

    LOG_WARN("Received I/O event for unknown fd: " << fd);
}

bool ReaderThread::accept(int serverFD, std::string& address)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = ::accept(serverFD, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    if (fd < 0)
    {
        LOG_WARN("Failed to accept new socket: " << strerror(errno));
        return false;
    }

    char ip[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == NULL) 
    {
        LOG_WARN("Failed to parse incoming connection IP address: " << strerror(errno));
        close(fd);
        return false;
    }

    address = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));

    if (!m_network.addClient(fd))
    {
        close(fd);
        return false;
    }

    return true;
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

std::shared_ptr<ConnectionHandle> ReaderThread::createHandle(int fd)
{
    auto disconnect = [&]() {
        ReaderThread::disconnect(fd);
    };

    auto send = [&](const std::string& msg) {
        m_network.m_writerThreads[fd % m_network.m_writerThreadCount]->send(fd, msg);
    };

    return std::make_unique<ConnectionHandle>(fd, disconnect, send);
}

void ReaderThread::addConnection(const MessageConsumer& consumer, int fd)
{

}

void ReaderThread::addAcceptor(const SessionSettings& settings, int fd)
{
    
}

////////////////////////////////////////////
//               ReadBuffer               //
////////////////////////////////////////////

std::vector<std::string> ReadBuffer::read(int fd)
{
    std::vector<std::string> ret;

    auto it = m_bufferMap.find(fd);
    if (it == m_bufferMap.end())
        it = m_bufferMap.insert({fd, ""}).first;
    std::string& buffer = it->second;

    int bytes = recv(fd, m_buffer, sizeof(m_buffer), 0);
    if (bytes <= 0)
        ; // TODO error
    buffer.append(m_buffer, bytes);

    // parse all the messages we can
    size_t ptr = 0, bodyLengthStart = 0;
    int bodyLength = 0;
    while (true)
    {
        // find beginning of message
        ptr = buffer.find(BEGIN_STRING_TAG, ptr);
        if (ptr == std::string::npos)
            break;
        if (ptr > 0)
        {
            LOG_WARN("Discarding text received in buffer: " << buffer.substr(0, ptr));
            buffer.erase(0, ptr);
            ptr = 0;
        }
        
        // find start of bodylength tag
        bodyLengthStart = buffer.find(BODY_LENGTH_TAG, ptr);
        if (bodyLengthStart == std::string::npos)
            break;

        ptr = buffer.find(INTERNAL_SOH_CHAR, bodyLengthStart);
        if (ptr == std::string::npos)
            break;

        try {
            bodyLength = std::stoi(buffer.substr(bodyLengthStart + BODY_LENGTH_TAG.size(), ptr - bodyLengthStart + BODY_LENGTH_TAG.size()));
            if (bodyLength < 0)
                throw std::runtime_error("Negative body length");
        } catch (...) {
            LOG_WARN("Unable to parse message, bad body length: " << buffer);
            // corrupted message, move up the buffer and hopefully splice it next round
            buffer.erase(0, ptr + 1);
            continue;
        }
            
        // find the checksum
        ptr = buffer.find(CHECKSUM_TAG, ptr);
        if (ptr == std::string::npos)
            break;
        ptr = buffer.find(INTERNAL_SOH_CHAR, ptr);
        if (ptr == std::string::npos)
            break;
        // completed message!
        ret.push_back(buffer.substr(0, ptr + 1));
        buffer.erase(0, ptr + 1);
    }

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

            int sent = 0;
            while (static_cast<size_t>(sent) < buffer.m_buffer.length())
            {
                int ret = ::send(fd, buffer.m_buffer.c_str() + sent, buffer.m_buffer.length(), 0);
                if (ret < 0)
                    ; // error
                sent += ret;
            }

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

void WriterThread::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running.store(false, std::memory_order_release);
    m_cv.notify_one();
}
