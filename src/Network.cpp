#include "Network.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <openfix/Utils.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>

#include "Exception.h"
#include "Fields.h"
#include "Message.h"

#define EVENT_BUF_SIZE 256
#define ACCEPTOR_BACKLOG 16

const std::string BEGIN_STRING_TAG = std::to_string(FIELD::BeginString) + TAG_ASSIGNMENT_CHAR;
const std::string BODY_LENGTH_TAG = std::to_string(FIELD::BodyLength);
const std::string SENDER_COMP_ID_TAG = std::to_string(FIELD::SenderCompID);
const std::string TARGET_COMP_ID_TAG = std::to_string(FIELD::TargetCompID);
const std::string SENDER_SUB_ID_TAG = std::to_string(FIELD::SenderSubID);
const std::string CHECKSUM_TAG = std::to_string(FIELD::CheckSum);

Network::Network()
    : m_epollFD(-1)
    , m_running(false)
{
    m_writerThreadCount = PlatformSettings::getLong(PlatformSettings::WRITER_THREADS);
    m_readerThreadCount = PlatformSettings::getLong(PlatformSettings::READER_THREADS);
}

bool try_make_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("NetUtils", "Error getting flags from socket: " << strerror(errno));
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("NetUtils", "Error setting non-blocking flag on socket: " << strerror(errno));
        return false;
    }

    return true;
}

bool set_sock_opt(int fd, int family, int optname, bool enable = true)
{
    int enable_flag = enable ? 1 : 0;
    if (setsockopt(fd, SOL_SOCKET, optname, &enable_flag, sizeof(enable_flag)) < 0) {
        LOG_ERROR("NetUtils", "Failed to set socket option " << optname << ": " << std::string(strerror(errno)));
    }
    return true;
}

void Network::start()
{
    if (m_running.load(std::memory_order_acquire))
        return;
    m_running.store(true, std::memory_order_release);

    LOG_INFO("Starting...");

    m_epollFD = epoll_create1(0);
    if (m_epollFD == -1) {
        throw std::runtime_error("Couldn't initialize epoll: " + std::string(strerror(errno)));
    }

    for (size_t i = 0; i < m_readerThreadCount; ++i)
        m_readerThreads.push_back(std::make_unique<ReaderThread>(*this));

    for (size_t i = 0; i < m_writerThreadCount; ++i)
        m_writerThreads.push_back(std::make_unique<WriterThread>(*this));

    LOG_INFO("Started, now running.");

    m_thread = std::thread([&] { run(); });
}

void Network::stop()
{
    if (!m_running.load(std::memory_order_acquire))
        return;
    m_running.store(false, std::memory_order_release);

    LOG_INFO("Stopping...");

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

    m_thread.join();

    // finally, close epoll FD
    close(m_epollFD);

    LOG_INFO("Stopped.");
}

bool Network::connect(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    std::string hostname = settings.getString(SessionSettings::CONNECT_HOST);
    int port = settings.getLong(SessionSettings::CONNECT_PORT);

    int fd = 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    struct addrinfo* ret;

    hints.ai_family = AF_UNSPEC;  // allow ipv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int err = ::getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &ret);
    if (err != 0) {
        LOG_ERROR("Error in hostname resolution: " << gai_strerror(err));
        return false;
    }

    long timeout = settings.getLong(SessionSettings::CONNECT_TIMEOUT);
    bool connected = false;

    for (struct addrinfo* addr = ret; addr != nullptr; addr = addr->ai_next) {
        fd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd == -1) {
            err = errno;
            continue;
        }

        if (!try_make_non_blocking(fd)) {
            close(fd);
            continue;
        }

        int res = ::connect(fd, addr->ai_addr, addr->ai_addrlen);
        if (res < 0 && errno == EINPROGRESS) {
            struct pollfd pfd;
            memset(&pfd, 0, sizeof(pfd));
            pfd.fd = fd;
            pfd.events = POLLOUT;

            res = poll(&pfd, 1, timeout);
            if (res > 0) {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    char host[NI_MAXHOST], service[NI_MAXSERV];
                    if (getnameinfo(addr->ai_addr, addr->ai_addrlen, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                        LOG_INFO("Successful connection to " << host << ":" << service << " on fd=" << fd);
                    } else {
                        LOG_WARN("Successful connection, but cannot resolve address to string...");
                    }

                    connected = true;
                    break;
                }
            }
        }

        close(fd);
        fd = -1;
    }

    ::freeaddrinfo(ret);

    if (connected) {
        if (!m_readerThreads[fd % m_readerThreadCount]->addConnection(handler, fd)) {
            close(fd);
            return false;
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLERR | EPOLLET;
        event.data.fd = fd;

        if (::epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0) {
            LOG_WARN("Failed to register connection with epoll: " << strerror(errno));
            close(fd);
            return false;
        }

        return true;
    }

    return false;
}

bool Network::hasAcceptor(const SessionSettings& settings)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    std::lock_guard lock(m_mutex);
    int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    auto it = m_acceptors.find(port);
    if (it != m_acceptors.end())
        return m_readerThreads[it->second % m_readerThreadCount]->hasAcceptor(settings.getSessionID(), it->second);
    return false;
}

bool Network::addAcceptor(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    // acceptors should be created/destroyed atomically
    std::lock_guard lock(m_mutex);

    int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    int fd = m_acceptors[port];

    // see if we already have a live fd associated with this accept port
    if (fd == 0) {
        if ((fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            LOG_ERROR("Unable to create socket: " << strerror(errno));
            return false;
        }

        if (!set_sock_opt(fd, SOL_SOCKET, SO_REUSEADDR)) {
            ::close(fd);
            return false;
        }

        LOG_DEBUG("Created server socket on port " << port << " with fd=" << fd);

        struct sockaddr_in addr;
        addr.sin_family = AF_UNSPEC;  // support ipv4
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERROR("Couldn't bind to port: " << strerror(errno));
            ::close(fd);
            return false;
        }

        if (::listen(fd, ACCEPTOR_BACKLOG) < 0) {
            LOG_ERROR("Couldn't listen to accept port: " << strerror(errno));
            ::close(fd);
            return false;
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLERR | EPOLLET;  // server socket should only notify once (ET)
        event.data.fd = fd;

        if (::epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0) {
            LOG_ERROR("Couldn't add listen socket to epoll wait list: " << strerror(errno));
            ::close(fd);
            return false;
        }

        // add to port->fd map
        m_acceptors[port] = fd;
    }

    m_readerThreads[fd % m_readerThreadCount]->addAcceptor(handler, settings.getSessionID(), fd);
    return true;
}

bool Network::removeAcceptor(const SessionSettings& settings)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    // acceptors should be created/destroyed atomically
    std::lock_guard lock(m_mutex);

    int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    int fd = m_acceptors[port];

    if (fd == 0) {
        // socket doesn't exist
        return false;
    }

    m_readerThreads[fd % m_readerThreadCount]->removeAcceptor(settings.getSessionID(), port);

    // remove from port->fd map
    m_acceptors.erase(port);

    return true;
}

void Network::run()
{
    struct epoll_event events[EVENT_BUF_SIZE];

    int numEvents;
    long timeout = PlatformSettings::getLong(PlatformSettings::EPOLL_TIMEOUT);

    while (m_running) {
        while ((numEvents = ::epoll_wait(m_epollFD, events, EVENT_BUF_SIZE, timeout)) > 0) {
            if (numEvents < 0) {
                if (errno == EINTR) {
                    LOG_WARN("epoll_wait was interrupted by a signal.");
                    continue;  // retry, no biggie
                }

                LOG_ERROR("epoll_wait error: " << strerror(errno));
                break;
            }

            if (numEvents == 0) {
                LOG_TRACE("epoll_wait timeout");
                continue;
            }

            for (int i = 0; i < numEvents; i++) {
                int fd = events[i].data.fd;
                uint32_t event_mask = events[i].events;

                if (event_mask & EPOLLERR) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
                        LOG_ERROR("EPOLLERR on fd=" << fd << ", error: " << strerror(err));
                    } else {
                        LOG_ERROR("EPOLLERR on fd=" << fd << ", and getsockopt failed to get error.");
                    }
                }
                if (event_mask & EPOLLIN) {
                    LOG_TRACE("data callback for fd=" << fd);

                    // data received
                    m_readerThreads[fd % m_readerThreadCount]->queue(fd);
                }
                if (event_mask & EPOLLOUT) {
                    LOG_TRACE("write callback for fd=" << fd);

                    // ready to write
                    m_writerThreads[fd % m_writerThreadCount]->notify();
                }
                if (event_mask & (EPOLLRDHUP | EPOLLHUP)) {
                    LOG_INFO("disconnect callback for fd=" << fd);
                    // connection hangup
                    m_readerThreads[fd % m_readerThreadCount]->disconnect(fd);
                    m_writerThreads[fd % m_writerThreadCount]->disconnect(fd);
                }
            }
        }
    }
}

bool Network::accept(int server_fd, const std::shared_ptr<Acceptor>& acceptor)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = ::accept(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    if (fd < 0) {
        LOG_WARN("Failed to accept new socket: " << strerror(errno));
        return -1;
    }

    if (!try_make_non_blocking(fd)) {
        ::close(fd);
        return false;
    }

    char ip[INET_ADDRSTRLEN + 1];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == NULL) {
        LOG_WARN("Failed to parse incoming connection IP address: " << ::strerror(errno));
        ::close(fd);
        return false;
    }

    std::string address = std::string(ip) + ":" + std::to_string(::ntohs(addr.sin_port));

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLERR | EPOLLET;
    event.data.fd = fd;

    if (::epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0) {
        LOG_WARN("Failed to register connection with epoll: " << strerror(errno));
        ::close(fd);
        return false;
    }

    LOG_INFO("Accepted new connection from fd=" << fd << " on server fd=" << server_fd << ": " << address);
    m_readerThreads[fd % m_readerThreads.size()]->accept(fd, acceptor);

    return true;
}

////////////////////////////////////////////
//              ReaderThread              //
////////////////////////////////////////////

void ReaderThread::queue(int fd)
{
    // lock as we insert to ensure data change is propogated to reader thread
    // we don't need to lock as we process
    {
        std::lock_guard lock(m_mutex);
        m_readyFDs.push(fd);
    }

    m_cv.notify_one();
}

void ReaderThread::disconnect(int fd)
{
    std::lock_guard lock(m_mutex);
    // immediately close the socket
    ::close(fd);
    m_buffer.clear(fd);
    auto it = m_connections.find(fd);
    if (m_connections.find(fd) != m_connections.end()) {
        LOG_DEBUG("Disconnecting known connection, fd=" << fd);
        it->second->invalidate();
        m_connections.erase(fd);
        return;
    }

    if (m_unknownConnections.find(fd) != m_unknownConnections.end()) {
        LOG_DEBUG("Disconnecting unknown connection, fd=" << fd);
        m_unknownConnections.erase(fd);
        return;
    }
}

void ReaderThread::process()
{
    while (m_running.load(std::memory_order_acquire)) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&]() { return !m_running.load() || !m_readyFDs.empty(); });
        lock.unlock();

        if (!m_running.load(std::memory_order_acquire))
            break;

        int fd;
        if (!m_readyFDs.try_pop(fd))
            continue;

        try {
            process(fd);
        } catch (const SocketClosedError& e) {
            LOG_ERROR("Socket is closed, fd=" << fd);
        }
    }
}

void ReaderThread::process(int fd)
{
    // lock here as we're touching connection maps
    std::lock_guard lock(m_mutex);

    // known connections
    {
        auto it = m_connections.find(fd);
        if (it != m_connections.end()) {
            LOG_TRACE("Handling data for known connection on fd=" << fd);

            auto msgs = m_buffer.read(fd);
            for (const auto& msg : msgs)
                it->second->processMessage(msg);

            return;
        }
    }

    // acceptor socket
    {
        auto it = m_acceptorSockets.find(fd);
        if (it != m_acceptorSockets.end()) {
            LOG_TRACE("Attempting to accept connection on accept socket fd=" << fd);
            m_network.accept(fd, it->second);
            return;
        }
    }

    // unknown acceptor connections
    {
        auto it = m_unknownConnections.find(fd);
        if (it != m_unknownConnections.end()) {
            LOG_TRACE("Handling data for unknown connection on fd=" << fd);
            auto acceptor = it->second;

            auto msgs = m_buffer.read(fd);
            if (!msgs.empty()) {
                // this connection is either invalid or will be known
                m_unknownConnections.erase(fd);
                const auto& msg = msgs[0];

                auto sender_comp = Utils::getTagValue(msg, SENDER_COMP_ID_TAG);
                if (sender_comp.first.empty()) {
                    LOG_ERROR("Received message without SenderCompID");
                    ::close(fd);
                    return;
                }

                auto target_comp = Utils::getTagValue(msg, TARGET_COMP_ID_TAG, sender_comp.second);
                if (target_comp.first.empty()) {
                    LOG_ERROR("Received message without TargetCompID");
                    ::close(fd);
                    return;
                }

                // flip as this is from their perspective
                auto cpty = target_comp.first + ':' + sender_comp.first;
                auto consumerIt = acceptor->m_sessions.find(cpty);
                if (consumerIt == acceptor->m_sessions.end()) {
                    LOG_ERROR("Received connection from unknown counterparty: " << cpty);
                    ::close(fd);
                    return;
                }

                // make sure this session isn't already connected
                if (consumerIt->second->isConnected()) {
                    LOG_ERROR("Received connection from already-connected session: " << cpty);
                    ::close(fd);
                    return;
                }

                // set socket settings
                consumerIt->second->setSocketSettings(fd);

                // create new connection
                LOG_DEBUG("Associating fd=" << fd << " with session: " << cpty);
                addConnection(consumerIt->second, fd);

                for (const auto& msg : msgs)
                    consumerIt->second->processMessage(msg);
            }

            return;
        }
    }

    LOG_WARN("Received I/O event for unknown fd: " << fd);
}

void ReaderThread::accept(int fd, const std::shared_ptr<Acceptor>& acceptor)
{
    std::lock_guard lock(m_mutex);
    m_unknownConnections[fd] = acceptor;
}

void ReaderThread::stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_running.store(false, std::memory_order_release);
    }

    m_cv.notify_one();

    // close open connections
    for (const auto& [k, conn] : m_connections) {
        ::close(k);
        // proper cleanup - maybe improve this in the future
        conn->invalidate();
    }
    // close unknown connections
    for (const auto& [k, _] : m_unknownConnections)
        ::close(k);
    // close acceptor sockets
    for (const auto& [k, _] : m_acceptorSockets) {
        LOG_DEBUG("Closing acceptor socket, fd=" << k);
        ::close(k);
    }

    m_connections.clear();
    m_unknownConnections.clear();
    m_acceptorSockets.clear();

    m_buffer.clear();
}

bool ReaderThread::addConnection(const std::shared_ptr<NetworkHandler>& handler, int fd)
{
    std::lock_guard lock(m_mutex);
    handler->setConnection(std::make_shared<ConnectionHandle>(m_network, *this, fd));
    m_connections[fd] = handler;

    return true;
}

bool ReaderThread::hasAcceptor(const SessionID_T sessionID, int fd)
{
    std::lock_guard lock(m_mutex);
    auto it = m_acceptorSockets.find(fd);
    if (it != m_acceptorSockets.end())
        return it->second->m_sessions.find(sessionID) != it->second->m_sessions.end();
    return false;
}

void ReaderThread::addAcceptor(const std::shared_ptr<NetworkHandler>& handler, const SessionID_T sessionID, int fd)
{
    std::lock_guard lock(m_mutex);
    auto it = m_acceptorSockets.find(fd);
    if (it == m_acceptorSockets.end())
        it = m_acceptorSockets.emplace(fd, std::make_shared<Acceptor>()).first;
    it->second->m_sessions[sessionID] = handler;
    LOG_DEBUG("Created acceptor socket for " << sessionID << " with fd=" << fd);
}

void ReaderThread::removeAcceptor(const SessionID_T sessionID, int fd)
{
    std::lock_guard lock(m_mutex);
    auto it = m_acceptorSockets.find(fd);
    if (it == m_acceptorSockets.end())
        return;
    it->second->m_sessions.erase(sessionID);
    if (it->second->m_sessions.empty())
        m_acceptorSockets.erase(fd);
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

    char read_buffer[READ_BUF_SIZE];

    int bytes = 0;
    while ((bytes = ::recv(fd, read_buffer, sizeof(read_buffer), 0)) > 0) {
        buffer.append(read_buffer, bytes);

        // parse all the messages we can
        size_t ptr = 0;
        while (true) {
            ptr = 0;

            // find beginning of message
            ptr = buffer.find(BEGIN_STRING_TAG, ptr);
            if (ptr == std::string::npos)
                break;

            if (ptr > 0) {
                LOG_WARN("Discarding text received in buffer: " << buffer.substr(0, ptr));
                buffer.erase(0, ptr);
                ptr = 0;
            }

            // find start of bodylength tag
            auto tag_it = Utils::getTagValue(buffer, BODY_LENGTH_TAG, ptr);
            if (tag_it.first.empty())
                break;
            ptr = tag_it.second;

            try {
                int bodyLength = std::stoi(tag_it.first);
                if (bodyLength < 0)
                    throw std::runtime_error("Negative body length");
                ptr += bodyLength;
            } catch (...) {
                LOG_WARN("Unable to parse message, bad body length: " << buffer);
                // corrupted message, move up the buffer and hopefully splice it next round
                buffer.erase(0, ptr + 1);
                continue;
            }

            // find the checksum
            tag_it = Utils::getTagValue(buffer, CHECKSUM_TAG, ptr);
            if (tag_it.first.empty())
                break;
            ptr = tag_it.second;

            // completed message!
            ret.push_back(buffer.substr(0, ptr + 1));
            buffer.erase(0, ptr + 1);
        }
    }

    if (bytes <= 0) {
        if (bytes == -1) {
            // nothing more to read for now, this is fine
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return ret;
            LOG_ERROR("Error reading from socket: " << std::string(strerror(errno)));
        } else {
            throw SocketClosedError("Socket is closed");
        }

        buffer.clear();
        return ret;
    }

    return ret;
}

////////////////////////////////////////////
//              WriterThread              //
////////////////////////////////////////////

void WriterThread::notify()
{
    std::lock_guard lock(m_mutex);
    m_cv.notify_one();
}

void WriterThread::process()
{
    while (m_running.load(std::memory_order_acquire)) {
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock);
        }

        if (!m_running.load(std::memory_order_acquire))
            return;

        std::vector<std::pair<int, WriteBuffer&>> buffers;
        {
            std::unique_lock lock(m_mutex);
            for (auto& [fd, buffer] : m_bufferMap) {
                if (!buffer.m_queue.empty())
                    buffer.m_queue.swap(buffer.m_buffer);
                if (!buffer.m_meta_queue.empty())
                    buffer.m_meta_queue.swap(buffer.m_meta_buffer);
                buffers.emplace_back(fd, buffer);
            }
        }

        for (auto& [fd, buffer] : buffers) {
            if (!buffer.m_valid.load(std::memory_order_acquire)) {
                std::unique_lock lock(m_mutex);
                m_bufferMap.erase(fd);
                continue;
            }

            if (buffer.m_buffer.empty())
                continue;

            size_t sent = 0;
            while (sent < buffer.m_buffer.length()) {
                ssize_t ret = ::send(fd, buffer.m_buffer.c_str() + sent, buffer.m_buffer.length() - sent, MSG_NOSIGNAL);
                if (ret < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        LOG_ERROR("Failed to send on fd=" << fd << ", will clear buffers: " << strerror(errno));
                        buffer.m_buffer.clear();
                        buffer.m_meta_buffer.clear();
                        break;
                    }
                    break;
                }
                sent += ret;
            }

            if (!buffer.m_meta_buffer.empty()) {
                size_t processed = 0;

                while (!buffer.m_meta_buffer.empty() && processed + buffer.m_meta_buffer.front().m_msg_size <= sent) {
                    processed += buffer.m_meta_buffer.front().m_msg_size;
                    if (buffer.m_meta_buffer.front().m_callback)
                        buffer.m_meta_buffer.front().m_callback();
                    buffer.m_meta_buffer.erase(buffer.m_meta_buffer.begin());
                }

                // if sent bytes did not complete the next message and partial data remains
                if (processed < sent && !buffer.m_meta_buffer.empty()) {
                    buffer.m_meta_buffer.front().m_msg_size -= (sent - processed);
                }
            }

            if (sent < buffer.m_buffer.length()) {
                // Not all data was sent; store the unsent part back in m_queue for later.
                std::lock_guard lock(m_mutex);
                buffer.m_queue.insert(0, buffer.m_buffer.substr(sent));
            }

            buffer.m_buffer.clear();
        }
    }
}

void WriterThread::send(int fd, MsgPacket&& msg)
{
    {
        std::lock_guard lock(m_mutex);
        m_bufferMap[fd].m_queue.append(msg.m_msg);
        m_bufferMap[fd].m_meta_queue.push_back(std::move(msg));
    }

    m_cv.notify_one();
}

void WriterThread::disconnect(int fd)
{
    {
        std::lock_guard lock(m_mutex);
        LOG_DEBUG("Disconnect received for fd=" << fd << ", clearing send buffer");
        m_bufferMap[fd].m_valid.store(false, std::memory_order_release);
    }

    m_cv.notify_one();
}

void WriterThread::stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_running.store(false, std::memory_order_release);
    }

    for (const auto& [fd, buf] : m_bufferMap)
        m_bufferMap[fd].m_valid = false;

    m_cv.notify_one();
}

////////////////////////////////////////////
//            ConnectionHandle            //
////////////////////////////////////////////

void ConnectionHandle::disconnect()
{
    m_readerThread.disconnect(m_fd);
    m_network.m_writerThreads[m_fd % m_network.m_writerThreadCount]->disconnect(m_fd);
}

void ConnectionHandle::send(MsgPacket&& msg)
{
    m_network.m_writerThreads[m_fd % m_network.m_writerThreadCount]->send(m_fd, std::move(msg));
}

////////////////////////////////////////////
//             NetworkHandler             //
////////////////////////////////////////////

void NetworkHandler::start()
{
    if (m_settings.getSessionType() == SessionType::INITIATOR) {
        LOG_INFO("Attempting to connect...");
        bool ret = m_network.connect(m_settings, shared_from_this());
        if (ret) {
            LOG_INFO("Successful connection.");
        } else {
            LOG_DEBUG("Unable to connect, will retry after next interval.");
        }
    } else if (m_settings.getSessionType() == SessionType::ACCEPTOR) {
        if (!m_network.hasAcceptor(m_settings)) {
            LOG_INFO("Attempting to create acceptor...");
            bool ret = m_network.addAcceptor(m_settings, shared_from_this());
            if (ret) {
                LOG_INFO("Successfully created acceptor.");
            } else {
                LOG_INFO("Unable to create acceptor.");
            }
        }
    }
}

void NetworkHandler::stop()
{
    // remove any existing connection
    disconnect();

    // shut down if we're an acceptor
    if (m_settings.getSessionType() == SessionType::ACCEPTOR) {
        m_network.removeAcceptor(m_settings);
    }
}

void NetworkHandler::setSocketSettings(int fd)
{
    set_sock_opt(fd, IPPROTO_TCP, TCP_NODELAY, m_settings.getBool(SessionSettings::ENABLE_TCP_NODELAY));
    set_sock_opt(fd, IPPROTO_TCP, TCP_QUICKACK, m_settings.getBool(SessionSettings::ENABLE_TCP_QUICKACK));
}

void NetworkHandler::processMessage(const std::string& msg)
{
    m_callback(msg);
}

void NetworkHandler::send(MsgPacket&& msg)
{
    std::lock_guard lock(m_mutex);
    if (m_connection) {
        if (!m_valid.load(std::memory_order_acquire)) {
            m_connection = nullptr;
            return;
        }
        m_connection->send(std::move(msg));
    }
}

void NetworkHandler::disconnect()
{
    std::lock_guard lock(m_mutex);
    if (m_connection) {
        if (!m_valid.load(std::memory_order_acquire)) {
            m_connection = nullptr;
            return;
        }
        m_connection->disconnect();
        m_connection = nullptr;
    }
}

void NetworkHandler::setConnection(std::shared_ptr<ConnectionHandle> connection)
{
    std::lock_guard lock(m_mutex);
    m_connection = connection;
    m_valid.store(true, std::memory_order_release);
}

void NetworkHandler::invalidate()
{
    m_valid.store(false, std::memory_order_release);
}

bool NetworkHandler::isConnected()
{
    std::lock_guard lock(m_mutex);
    if (!m_valid.load(std::memory_order_acquire)) {
        m_connection = nullptr;
        return false;
    }
    return m_connection != nullptr;
}
