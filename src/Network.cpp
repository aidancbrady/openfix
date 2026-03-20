#include "Network.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openfix/CpuOrchestrator.h>
#include <openfix/Utils.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <sstream>

#include "Exception.h"
#include "Fields.h"
#include "Message.h"

#define EVENT_BUF_SIZE 256
#define ACCEPTOR_BACKLOG 16

const std::string BEGIN_STRING_TAG = std::to_string(FIELD::BeginString) + TAG_ASSIGNMENT_CHAR;

// pre-built search patterns: SOH + tag + '='
const std::string BODY_LENGTH_PATTERN = Utils::buildTagPattern(FIELD::BodyLength);
const std::string SENDER_COMP_ID_PATTERN = Utils::buildTagPattern(FIELD::SenderCompID);
const std::string TARGET_COMP_ID_PATTERN = Utils::buildTagPattern(FIELD::TargetCompID);
const std::string CHECKSUM_PATTERN = Utils::buildTagPattern(FIELD::CheckSum);

Network::Network()
    : m_running(false)
{
    static const int init_result = OPENSSL_init_ssl(0, nullptr);
    if (init_result != 1) {
        throw std::runtime_error("Failed to initialize OpenSSL");
    }

    m_readerThreadCount = PlatformSettings::getLong(PlatformSettings::READER_THREADS);
}

bool try_make_non_blocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
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
    const int enable_flag = enable ? 1 : 0;
    if (setsockopt(fd, family, optname, &enable_flag, sizeof(enable_flag)) < 0) {
        LOG_ERROR("NetUtils", "Failed to set socket option " << optname << ": " << std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool is_tls_enabled(const SessionSettings& settings)
{
    return settings.getBool(SessionSettings::TLS_ENABLED);
}

std::string get_ssl_error()
{
    const unsigned long err = ERR_get_error();
    if (err == 0)
        return "unknown SSL error";
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(buf);
}

void Network::start()
{
    if (m_running.load(std::memory_order_acquire))
        return;
    m_running.store(true, std::memory_order_release);

    LOG_INFO("Starting...");

    for (size_t i = 0; i < m_readerThreadCount; ++i)
        m_readerThreads.push_back(std::make_unique<ReaderThread>(*this));

    LOG_INFO("Started, now running.");
}

void Network::stop()
{
    if (!m_running.load(std::memory_order_acquire))
        return;
    m_running.store(false, std::memory_order_release);

    LOG_INFO("Stopping...");

    for (auto& thread : m_readerThreads)
        thread->stop();

    for (auto& thread : m_readerThreads)
        thread->join();

    m_readerThreads.clear();

    {
        std::lock_guard lock(m_tlsMutex);
        for (auto& [_, conn] : m_tlsConnections) {
            std::lock_guard connLock(conn->m_mutex);
            if (conn->m_ssl)
                SSL_free(conn->m_ssl);
        }
        m_tlsConnections.clear();
        m_tlsContexts.clear();
        m_hasTLSConnections.store(false, std::memory_order_release);
    }

    LOG_INFO("Stopped.");
}

bool Network::connect(const SessionSettings& settings, const std::shared_ptr<NetworkHandler>& handler)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    const std::string hostname = settings.getString(SessionSettings::CONNECT_HOST);
    const int port = settings.getLong(SessionSettings::CONNECT_PORT);

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

    const long timeout = settings.getLong(SessionSettings::CONNECT_TIMEOUT);
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
        if (res == 0) {
            connected = true;
            break;
        } else if (res < 0 && errno == EINPROGRESS) {
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
        handler->setSocketSettings(fd);

        if (is_tls_enabled(settings)) {
            std::string tlsServerName = settings.getString(SessionSettings::TLS_SERVER_NAME);
            if (tlsServerName.empty())
                tlsServerName = hostname;
            if (!createTLSConnection(fd, settings, false, tlsServerName)) {
                close(fd);
                return false;
            }
        }

        auto& reader = *m_readerThreads[fd % m_readerThreadCount];
        if (!reader.addConnection(handler, fd)) {
            removeConnection(fd);
            close(fd);
            return false;
        }

        reader.registerFD(fd);
        return true;
    }

    return false;
}

bool Network::hasAcceptor(const SessionSettings& settings)
{
    if (!m_running.load(std::memory_order_acquire))
        return false;

    std::lock_guard lock(m_mutex);
    const int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    const auto it = m_acceptors.find(port);
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

    const int port = settings.getLong(SessionSettings::ACCEPT_PORT);
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
        addr.sin_family = AF_INET;
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

        // add to port->fd map
        m_acceptors[port] = fd;

        // register listen socket with owning reader's epoll (EPOLLIN only, ET)
        auto& reader = *m_readerThreads[fd % m_readerThreadCount];
        reader.addAcceptor(handler, settings.getSessionID(), fd);
        reader.registerFD(fd);

        return true;
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

    const int port = settings.getLong(SessionSettings::ACCEPT_PORT);
    const int fd = m_acceptors[port];

    if (fd == 0) {
        // socket doesn't exist
        return false;
    }

    const bool wasLastSession = m_readerThreads[fd % m_readerThreadCount]->removeAcceptor(settings.getSessionID(), fd);

    if (wasLastSession) {
    	// remove from port->fd map
    	m_acceptors.erase(port);
        ::close(fd);
    }

    return true;
}

bool Network::accept(int server_fd, const std::shared_ptr<Acceptor>& acceptor)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    const int fd = ::accept(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    if (fd < 0) {
        LOG_WARN("Failed to accept new socket: " << strerror(errno));
        return false;
    }

    if (!try_make_non_blocking(fd)) {
        ::close(fd);
        return false;
    }

    char ip[INET_ADDRSTRLEN + 1];
    if (::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == NULL) {
        LOG_WARN("Failed to parse incoming connection IP address: " << strerror(errno));
        ::close(fd);
        return false;
    }

    const std::string address = std::string(ip) + ":" + std::to_string(::ntohs(addr.sin_port));

    if (!acceptor->m_sessions.empty()) {
        const SessionSettings& settings = acceptor->m_sessions.begin()->second->getSettings();
        if (is_tls_enabled(settings) && !createTLSConnection(fd, settings, true, "")) {
            ::close(fd);
            return false;
        }
    }

    LOG_INFO("Accepted new connection from fd=" << fd << " on server fd=" << server_fd << ": " << address);

    // Register with the owning reader: unknown connection first, then epoll
    auto& reader = *m_readerThreads[fd % m_readerThreads.size()];
    reader.accept(fd, acceptor);
    reader.registerFD(fd);

    return true;
}

bool Network::createTLSConnection(int fd, const SessionSettings& settings, bool serverMode, const std::string& serverName)
{
    auto ctx = getTLSContext(settings, serverMode);
    if (!ctx)
        return false;

    std::shared_ptr<TLSConnection> conn = std::make_shared<TLSConnection>();
    conn->m_ctx = std::move(ctx);
    conn->m_serverMode = serverMode;
    conn->m_ssl = SSL_new(conn->m_ctx.get());
    if (!conn->m_ssl) {
        LOG_ERROR("Failed to create SSL object for fd=" << fd << ": " << get_ssl_error());
        return false;
    }

    if (SSL_set_fd(conn->m_ssl, fd) != 1) {
        LOG_ERROR("Failed to attach SSL object to fd=" << fd << ": " << get_ssl_error());
        SSL_free(conn->m_ssl);
        conn->m_ssl = nullptr;
        return false;
    }

    if (serverMode) {
        SSL_set_accept_state(conn->m_ssl);
    } else {
        SSL_set_connect_state(conn->m_ssl);
        if (!serverName.empty()) {
            SSL_set_tlsext_host_name(conn->m_ssl, serverName.c_str());
        }

        if (settings.getBool(SessionSettings::TLS_VERIFY_PEER) && !serverName.empty()) {
            X509_VERIFY_PARAM* verifyParams = SSL_get0_param(conn->m_ssl);
            if (!verifyParams) {
                LOG_ERROR("Failed to acquire TLS verify params for fd=" << fd);
                SSL_free(conn->m_ssl);
                conn->m_ssl = nullptr;
                return false;
            }

            bool verifyTargetSet = false;
            if (X509_VERIFY_PARAM_set1_ip_asc(verifyParams, serverName.c_str()) == 1)
                verifyTargetSet = true;
            else if (SSL_set1_host(conn->m_ssl, serverName.c_str()) == 1)
                verifyTargetSet = true;

            if (!verifyTargetSet) {
                LOG_ERROR("Failed to set TLS hostname verification target for fd=" << fd);
                SSL_free(conn->m_ssl);
                conn->m_ssl = nullptr;
                return false;
            }
        }
    }

    std::lock_guard lock(m_tlsMutex);
    m_tlsConnections[fd] = std::move(conn);
    m_hasTLSConnections.store(true, std::memory_order_release);
    return true;
}

std::string Network::getTLSContextKey(const SessionSettings& settings, bool serverMode) const
{
    std::stringstream ss;
    ss << (serverMode ? "S:" : "C:") << settings.getSessionID() << ':'
       << settings.getString(SessionSettings::TLS_CA_FILE) << ':'
       << settings.getString(SessionSettings::TLS_CERT_FILE) << ':'
       << settings.getString(SessionSettings::TLS_KEY_FILE) << ':'
       << settings.getBool(SessionSettings::TLS_VERIFY_PEER) << ':'
       << settings.getBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT);
    return ss.str();
}

std::shared_ptr<SSL_CTX> Network::getTLSContext(const SessionSettings& settings, bool serverMode)
{
    const std::string key = getTLSContextKey(settings, serverMode);
    std::lock_guard lock(m_tlsContextsMutex);
    const auto it = m_tlsContexts.find(key);
    if (it != m_tlsContexts.end())
        return it->second;

    const auto ctx = createTLSContext(settings, serverMode);
    if (ctx)
        m_tlsContexts[key] = ctx;
    return ctx;
}

std::shared_ptr<SSL_CTX> Network::createTLSContext(const SessionSettings& settings, bool serverMode) const
{
    SSL_CTX* rawCtx = SSL_CTX_new(TLS_method());
    if (!rawCtx) {
        LOG_ERROR("Failed to create SSL_CTX: " << get_ssl_error());
        return nullptr;
    }

    std::shared_ptr<SSL_CTX> ctx(rawCtx, [](SSL_CTX* c) {
        if (c)
            SSL_CTX_free(c);
    });

    SSL_CTX_set_min_proto_version(rawCtx, TLS1_2_VERSION);
    SSL_CTX_set_mode(rawCtx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);
    SSL_CTX_set_options(rawCtx, SSL_OP_NO_COMPRESSION);

    const std::string caFile = settings.getString(SessionSettings::TLS_CA_FILE);
    const std::string certFile = settings.getString(SessionSettings::TLS_CERT_FILE);
    const std::string keyFile = settings.getString(SessionSettings::TLS_KEY_FILE);
    const bool verifyPeer = settings.getBool(SessionSettings::TLS_VERIFY_PEER);
    const bool requireClientCert = settings.getBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT);

    if (!caFile.empty()) {
        if (SSL_CTX_load_verify_locations(rawCtx, caFile.c_str(), nullptr) != 1) {
            LOG_ERROR("Failed to load TLS CA file: " << caFile << " (" << get_ssl_error() << ")");
            return nullptr;
        }
    } else if (verifyPeer || requireClientCert) {
        if (SSL_CTX_set_default_verify_paths(rawCtx) != 1) {
            LOG_ERROR("Failed to load default TLS trust store: " << get_ssl_error());
            return nullptr;
        }
    }

    if (!certFile.empty()) {
        if (SSL_CTX_use_certificate_file(rawCtx, certFile.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_ERROR("Failed to load TLS certificate file: " << certFile << " (" << get_ssl_error() << ")");
            return nullptr;
        }
    }

    if (!keyFile.empty()) {
        if (SSL_CTX_use_PrivateKey_file(rawCtx, keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_ERROR("Failed to load TLS private key file: " << keyFile << " (" << get_ssl_error() << ")");
            return nullptr;
        }
    }

    if (!certFile.empty() || !keyFile.empty()) {
        if (SSL_CTX_check_private_key(rawCtx) != 1) {
            LOG_ERROR("TLS certificate/private key mismatch: " << get_ssl_error());
            return nullptr;
        }
    }

    if (serverMode && (certFile.empty() || keyFile.empty())) {
        LOG_ERROR("TLS acceptor mode requires both TLSCertFile and TLSKeyFile");
        return nullptr;
    }

    if (serverMode) {
        if (requireClientCert) {
            SSL_CTX_set_verify(rawCtx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        } else if (verifyPeer) {
            SSL_CTX_set_verify(rawCtx, SSL_VERIFY_PEER, nullptr);
        } else {
            SSL_CTX_set_verify(rawCtx, SSL_VERIFY_NONE, nullptr);
        }
    } else {
        SSL_CTX_set_verify(rawCtx, verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    }

    return ctx;
}

bool Network::progressConnection(int fd)
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return true;

    std::shared_ptr<TLSConnection> conn;
    {
        std::shared_lock lock(m_tlsMutex);
        auto it = m_tlsConnections.find(fd);
        if (it == m_tlsConnections.end())
            return true;
        conn = it->second;
    }

    std::lock_guard connLock(conn->m_mutex);
    if (conn->m_ready.load(std::memory_order_acquire))
        return true;

    const int ret = conn->m_serverMode ? SSL_accept(conn->m_ssl) : SSL_connect(conn->m_ssl);
    if (ret == 1) {
        conn->m_ready.store(true, std::memory_order_release);
        LOG_INFO("TLS handshake complete for fd=" << fd << ", version=" << SSL_get_version(conn->m_ssl) << ", cipher=" << SSL_get_cipher(conn->m_ssl));
        return true;
    }

    const int sslErr = SSL_get_error(conn->m_ssl, ret);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE)
        return true;

    LOG_ERROR("TLS handshake failed on fd=" << fd << ": " << get_ssl_error());
    return false;
}

bool Network::isConnectionReady(int fd) const
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return true;

    std::shared_lock lock(m_tlsMutex);
    auto it = m_tlsConnections.find(fd);
    if (it == m_tlsConnections.end())
        return true;
    return it->second->m_ready.load(std::memory_order_acquire);
}

bool Network::requiresConnectionProgress(int fd) const
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return false;

    std::shared_lock lock(m_tlsMutex);
    auto it = m_tlsConnections.find(fd);
    if (it == m_tlsConnections.end())
        return false;
    return !it->second->m_ready.load(std::memory_order_acquire);
}

bool Network::hasTLS(int fd) const
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return false;

    std::shared_lock lock(m_tlsMutex);
    return m_tlsConnections.find(fd) != m_tlsConnections.end();
}

ssize_t Network::readConnection(int fd, void* buf, size_t len)
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return ::recv(fd, buf, len, 0);

    std::shared_ptr<TLSConnection> conn;
    {
        std::shared_lock lock(m_tlsMutex);
        auto it = m_tlsConnections.find(fd);
        if (it == m_tlsConnections.end())
            return ::recv(fd, buf, len, 0);
        conn = it->second;
    }

    if (!conn->m_ready.load(std::memory_order_acquire)) {
        errno = EAGAIN;
        return -1;
    }

    std::lock_guard connLock(conn->m_mutex);

    const int ret = SSL_read(conn->m_ssl, buf, static_cast<int>(len));
    if (ret > 0)
        return ret;

    const int sslErr = SSL_get_error(conn->m_ssl, ret);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        errno = EAGAIN;
        return -1;
    }
    if (sslErr == SSL_ERROR_ZERO_RETURN)
        return 0;

    LOG_ERROR("TLS read failed on fd=" << fd << ": " << get_ssl_error());
    return 0;
}

ssize_t Network::writeConnection(int fd, const void* buf, size_t len)
{
    if (!m_hasTLSConnections.load(std::memory_order_acquire))
        return ::send(fd, buf, len, MSG_NOSIGNAL);

    std::shared_ptr<TLSConnection> conn;
    {
        std::shared_lock lock(m_tlsMutex);
        auto it = m_tlsConnections.find(fd);
        if (it == m_tlsConnections.end())
            return ::send(fd, buf, len, MSG_NOSIGNAL);
        conn = it->second;
    }

    if (!conn->m_ready.load(std::memory_order_acquire)) {
        errno = EAGAIN;
        return -1;
    }

    std::lock_guard connLock(conn->m_mutex);

    const int ret = SSL_write(conn->m_ssl, buf, static_cast<int>(len));
    if (ret > 0)
        return ret;

    const int sslErr = SSL_get_error(conn->m_ssl, ret);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        errno = EAGAIN;
        return -1;
    }

    LOG_ERROR("TLS write failed on fd=" << fd << ": " << get_ssl_error());
    errno = ECONNRESET;
    return -1;
}

void Network::removeConnection(int fd)
{
    std::shared_ptr<TLSConnection> conn;
    {
        std::lock_guard lock(m_tlsMutex);
        auto it = m_tlsConnections.find(fd);
        if (it == m_tlsConnections.end())
            return;
        conn = it->second;
        m_tlsConnections.erase(it);
        if (m_tlsConnections.empty())
            m_hasTLSConnections.store(false, std::memory_order_release);
    }

    std::lock_guard connLock(conn->m_mutex);
    if (conn->m_ssl)
        SSL_free(conn->m_ssl);
    conn->m_ssl = nullptr;
}

////////////////////////////////////////////
//              ReaderThread              //
////////////////////////////////////////////

ReaderThread::ReaderThread(Network& network)
    : m_running(true)
    , m_buffer(network)
    , m_network(network)
{
    m_epollFD = epoll_create1(0);
    if (m_epollFD == -1)
        throw std::runtime_error("ReaderThread: epoll_create1 failed: " + std::string(strerror(errno)));

    m_eventFD = eventfd(0, EFD_NONBLOCK);
    if (m_eventFD == -1)
        throw std::runtime_error("ReaderThread: eventfd failed: " + std::string(strerror(errno)));

    // Register eventfd with level-triggered (not ET) so we never miss a wakeup
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = m_eventFD;
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_eventFD, &ev) < 0)
        throw std::runtime_error("ReaderThread: failed to register eventfd: " + std::string(strerror(errno)));

    m_thread = std::thread([&] {
        CpuOrchestrator::bind(ThreadRole::READER);
        process();
    });
}

ReaderThread::~ReaderThread()
{
    if (m_epollFD >= 0)
        ::close(m_epollFD);
    if (m_eventFD >= 0)
        ::close(m_eventFD);
}

void ReaderThread::registerFD(int fd)
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLERR | EPOLLET;
    event.data.fd = fd;

    if (::epoll_ctl(m_epollFD, EPOLL_CTL_ADD, fd, &event) < 0) {
        LOG_WARN("Failed to register fd=" << fd << " with reader epoll: " << strerror(errno));
    }
}

void ReaderThread::process()
{
    struct epoll_event events[EVENT_BUF_SIZE];
    const long timeout = PlatformSettings::getLong(PlatformSettings::EPOLL_TIMEOUT);

    while (m_running.load(std::memory_order_acquire)) {
        const int n = ::epoll_wait(m_epollFD, events, EVENT_BUF_SIZE, timeout);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERROR("epoll_wait error: " << strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t mask = events[i].events;

            // Handle eventfd wakeup: flush pending writes
            if (fd == m_eventFD) {
                uint64_t val;
                ::read(m_eventFD, &val, sizeof(val));
                flushWrites();
                continue;
            }

            // Handle errors
            if (mask & EPOLLERR) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
                    LOG_ERROR("EPOLLERR on fd=" << fd << ", error: " << strerror(err));
                } else {
                    LOG_ERROR("EPOLLERR on fd=" << fd << ", and getsockopt failed to get error.");
                }
            }

            // Handle disconnect
            if (mask & (EPOLLHUP | EPOLLRDHUP)) {
                LOG_INFO("disconnect callback for fd=" << fd);
                disconnect(fd);
                continue;
            }

            // Progress TLS handshake if needed
            if (m_network.requiresConnectionProgress(fd)) {
                if (!m_network.progressConnection(fd)) {
                    disconnect(fd);
                    continue;
                }
                if (m_network.requiresConnectionProgress(fd))
                    continue;  // still in progress, wait for more events
            }

            // Handle readable data
            if (mask & EPOLLIN) {
                try {
                    processRead(fd);
                } catch (const SocketClosedError& e) {
                    LOG_ERROR("Socket is closed, fd=" << fd);
                }
            }

            // Handle writable: flush pending write buffers
            if (mask & EPOLLOUT) {
                std::lock_guard lock(m_writeMutex);
                auto it = m_writeBuffers.find(fd);
                if (it != m_writeBuffers.end())
                    flushWrite(fd, it->second);
            }
        }

        // Tick all connected sessions (time-gated internally).
        // Snapshot under the lock: update() can re-enter network code and
        // mutate m_connections, which would invalidate the iterator.
        std::vector<std::shared_ptr<NetworkHandler>> handlers;
        {
            std::lock_guard lock(m_mutex);
            handlers.reserve(m_connections.size());
            for (const auto& [_, handler] : m_connections)
                handlers.push_back(handler);
        }
        for (const auto& handler : handlers)
            handler->update();
    }

    // Reader thread is exiting — safe to clear buffers with no concurrent access
    m_buffer.clear();
}

void ReaderThread::processRead(int fd)
{
    // Fast path: look up the handler and read data under the lock, then release
    // before message processing. The lock must cover m_buffer.read(fd) because
    // external disconnect() can erase from m_bufferMap under m_mutex — without
    // the lock, read()'s reference into the map would dangle.
    //
    // Raw pointer is safe here: the Session holds a shared_ptr to the NetworkHandler,
    // so it stays alive for the duration of message processing on the reader thread.
    NetworkHandler* handler = nullptr;
    std::vector<std::string>* msgs = nullptr;
    {
        std::lock_guard lock(m_mutex);

        const auto it = m_connections.find(fd);
        if (it != m_connections.end()) {
            handler = it->second.get();
            msgs = &m_buffer.read(fd);
        }
    }

    // Known connection: process outside the lock (msgs points to m_readResult,
    // which is stable until the next read() call — only happens on this thread)
    if (handler) {
        LOG_TRACE("Handling data for known connection on fd=" << fd);

        for (auto& msg : *msgs)
            handler->processMessage(std::move(msg));

        return;
    }

    // Cold paths (connection setup): re-acquire lock for map mutations
    std::lock_guard lock(m_mutex);

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
            const auto acceptor = it->second;

            auto& msgs = m_buffer.read(fd);
            if (!msgs.empty()) {
                // this connection is either invalid or will be known
                m_unknownConnections.erase(fd);
                const auto& msg = msgs[0];

                const auto sender_comp = Utils::getTagValue(msg, SENDER_COMP_ID_PATTERN, SENDER_COMP_ID_PATTERN.size(), 0);
                if (sender_comp.first.empty()) {
                    LOG_ERROR("Received message without SenderCompID");
                    ::close(fd);
                    return;
                }

                const auto target_comp = Utils::getTagValue(msg, TARGET_COMP_ID_PATTERN, TARGET_COMP_ID_PATTERN.size(), sender_comp.second);
                if (target_comp.first.empty()) {
                    LOG_ERROR("Received message without TargetCompID");
                    ::close(fd);
                    return;
                }

                // flip as this is from their perspective
                const auto cpty = target_comp.first + ':' + sender_comp.first;
                const auto consumerIt = acceptor->m_sessions.find(cpty);
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

                for (auto& msg : msgs)
                    consumerIt->second->processMessage(std::move(msg));
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
    m_running.store(false, std::memory_order_release);

    // Wake the reader from epoll_wait via eventfd
    uint64_t val = 1;
    ::write(m_eventFD, &val, sizeof(val));

    // close open connections
    {
        std::lock_guard lock(m_mutex);
        for (const auto& [k, conn] : m_connections) {
            ::close(k);
            m_network.removeConnection(k);
            conn->invalidate();
        }
        for (const auto& [k, _] : m_unknownConnections) {
            ::close(k);
            m_network.removeConnection(k);
        }
        for (const auto& [k, _] : m_acceptorSockets) {
            LOG_DEBUG("Closing acceptor socket, fd=" << k);
            ::close(k);
            m_network.removeConnection(k);
        }

        m_connections.clear();
        m_unknownConnections.clear();
        m_acceptorSockets.clear();
    }

    {
        std::lock_guard lock(m_writeMutex);
        m_writeBuffers.clear();
    }
}

bool ReaderThread::addConnection(const std::shared_ptr<NetworkHandler>& handler, int fd)
{
    std::lock_guard lock(m_mutex);
    handler->setConnection(std::make_shared<ConnectionHandle>(m_network, *this, fd, m_network.hasTLS(fd)));
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

bool ReaderThread::removeAcceptor(const SessionID_T sessionID, int fd)
{
    std::lock_guard lock(m_mutex);
    const auto it = m_acceptorSockets.find(fd);
    if (it == m_acceptorSockets.end())
        return true;
    it->second->m_sessions.erase(sessionID);
    if (it->second->m_sessions.empty()) {
        m_acceptorSockets.erase(fd);
        return true;
    }
    return false;
}

void ReaderThread::disconnect(int fd)
{
    {
        std::lock_guard lock(m_mutex);
        ::close(fd);
        m_network.removeConnection(fd);
        m_buffer.clear(fd);
        const auto it = m_connections.find(fd);
        if (m_connections.find(fd) != m_connections.end()) {
            LOG_DEBUG("Disconnecting known connection, fd=" << fd);
            it->second->invalidate();
            m_connections.erase(fd);
        } else if (m_unknownConnections.find(fd) != m_unknownConnections.end()) {
            LOG_DEBUG("Disconnecting unknown connection, fd=" << fd);
            m_unknownConnections.erase(fd);
        }
    }

    // Clean up write buffers
    {
        std::lock_guard lock(m_writeMutex);
        m_writeBuffers.erase(fd);
    }
}

////////////////////////////////////////////
//               ReadBuffer               //
////////////////////////////////////////////

std::vector<std::string>& ReadBuffer::read(int fd)
{
    m_readResult.clear();

    auto it = m_bufferMap.find(fd);
    if (it == m_bufferMap.end())
        it = m_bufferMap.insert({fd, ""}).first;
    std::string& buffer = it->second;

    char read_buffer[READ_BUF_SIZE];

    int bytes = 0;
    while ((bytes = m_network.readConnection(fd, read_buffer, sizeof(read_buffer))) > 0) {
        buffer.append(read_buffer, bytes);

        // parse all the messages we can, tracking consumed bytes via offset
        size_t consumed = 0;
        while (true) {
            // find beginning of message
            const size_t ptr_start = buffer.find(BEGIN_STRING_TAG, consumed);
            if (ptr_start == std::string::npos)
                break;

            if (ptr_start > consumed) {
                LOG_WARN("Discarding text received in buffer: " << buffer.substr(consumed, ptr_start - consumed));
                consumed = ptr_start;
            }

            size_t ptr = ptr_start;

            // find start of bodylength tag (zero-copy)
            auto tag_it = Utils::getTagValueView(buffer, BODY_LENGTH_PATTERN, BODY_LENGTH_PATTERN.size(), ptr);
            if (tag_it.first.empty())
                break;
            ptr = tag_it.second;

            {
                int bodyLength = 0;
                const auto [p, ec] = std::from_chars(tag_it.first.data(), tag_it.first.data() + tag_it.first.size(), bodyLength);
                if (ec != std::errc{} || bodyLength < 0) {
                    LOG_WARN("Unable to parse message, bad body length: " << buffer.substr(consumed, ptr + 1 - consumed));
                    consumed = ptr + 1;
                    continue;
                }
                ptr += bodyLength;
            }

            // find the checksum (zero-copy, we only need the position)
            tag_it = Utils::getTagValueView(buffer, CHECKSUM_PATTERN, CHECKSUM_PATTERN.size(), ptr);
            if (tag_it.first.empty())
                break;
            ptr = tag_it.second;

            // completed message!
            const size_t msgEnd = ptr + 1;
            if (consumed == 0 && msgEnd == buffer.size()) {
                // Fast path: single message consumed the entire buffer — move instead of copy.
                m_readResult.push_back(std::move(buffer));
                buffer.clear();
                consumed = 0; // buffer is now empty, skip erase below
                break;
            }
            m_readResult.push_back(buffer.substr(consumed, msgEnd - consumed));
            consumed = msgEnd;
        }

        // compact buffer once after extracting all messages
        if (consumed > 0)
            buffer.erase(0, consumed);
    }

    if (bytes <= 0) {
        if (bytes == -1) {
            // nothing more to read for now, this is fine
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return m_readResult;
            LOG_ERROR("Error reading from socket: " << std::string(strerror(errno)));
        } else {
            throw SocketClosedError("Socket is closed");
        }

        buffer.clear();
        return m_readResult;
    }

    return m_readResult;
}

////////////////////////////////////////////
//            Write path                  //
////////////////////////////////////////////

bool ReaderThread::trySend(int fd, MsgPacket& msg)
{
    std::unique_lock lock(m_writeMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return false;

    // only inline send when there's no pending data — preserves message ordering
    const auto it = m_writeBuffers.find(fd);
    if (it != m_writeBuffers.end()) {
        if (!it->second.m_valid.load(std::memory_order_acquire))
            return false;
        if (!it->second.m_queue.empty() || !it->second.m_drain.empty())
            return false;
    }

    const ssize_t ret = m_network.writeConnection(fd, msg.m_msg.c_str(), msg.m_msg.size());

    if (ret == static_cast<ssize_t>(msg.m_msg.size()))
        return true;

    // partial send — trim what was sent so the caller queues only the remainder
    if (ret > 0)
        msg.m_msg.erase(0, ret);

    return false;
}

void ReaderThread::queueWrite(int fd, MsgPacket&& msg)
{
    {
        std::lock_guard lock(m_writeMutex);
        m_writeBuffers[fd].m_queue.push_back({std::move(msg.m_msg), std::move(msg.m_callback)});
    }

    // wake reader to flush
    uint64_t val = 1;
    ::write(m_eventFD, &val, sizeof(val));
}

void ReaderThread::flushWrites()
{
    std::lock_guard lock(m_writeMutex);
    for (auto it = m_writeBuffers.begin(); it != m_writeBuffers.end(); ) {
        if (!it->second.m_valid.load(std::memory_order_acquire)) {
            it = m_writeBuffers.erase(it);
            continue;
        }
        flushWrite(it->first, it->second);
        ++it;
    }
}

void ReaderThread::flushWrite(int fd, WriteBuffer& wb)
{
    // Swap incoming queue into drain buffer if drain is empty
    if (wb.m_drain.empty() && !wb.m_queue.empty()) {
        wb.m_drain.swap(wb.m_queue);
        wb.m_offset = 0;
    }

    if (wb.m_drain.empty())
        return;

    if (m_network.hasTLS(fd)) {
        // TLS: write each message individually via SSL_write
        while (!wb.m_drain.empty()) {
            auto& entry = wb.m_drain.front();
            const ssize_t ret = m_network.writeConnection(fd, entry.m_msg.data() + wb.m_offset, entry.m_msg.size() - wb.m_offset);
            if (ret <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                LOG_ERROR("TLS write failed during flush on fd=" << fd << ": " << strerror(errno));
                wb.m_drain.clear();
                wb.m_queue.clear();
                return;
            }
            wb.m_offset += ret;
            if (wb.m_offset >= entry.m_msg.size()) {
                if (entry.m_callback)
                    entry.m_callback();
                wb.m_drain.pop_front();
                wb.m_offset = 0;
            } else {
                return;  // partial write, wait for next EPOLLOUT
            }
        }
    } else {
        // Non-TLS: vectorized send with writev()
        const int count = static_cast<int>(std::min(wb.m_drain.size(), static_cast<size_t>(MAX_WRITE_IOVECS)));
        struct iovec iovs[MAX_WRITE_IOVECS];

        for (int i = 0; i < count; ++i) {
            auto& entry = wb.m_drain[i];
            if (i == 0) {
                iovs[i].iov_base = const_cast<char*>(entry.m_msg.data()) + wb.m_offset;
                iovs[i].iov_len = entry.m_msg.size() - wb.m_offset;
            } else {
                iovs[i].iov_base = const_cast<char*>(entry.m_msg.data());
                iovs[i].iov_len = entry.m_msg.size();
            }
        }

        const ssize_t ret = ::writev(fd, iovs, count);
        if (ret <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            LOG_ERROR("writev failed on fd=" << fd << ": " << strerror(errno));
            wb.m_drain.clear();
            wb.m_queue.clear();
            return;
        }

        // Account for sent bytes, fire callbacks for completed messages
        size_t sent = static_cast<size_t>(ret);
        while (!wb.m_drain.empty() && sent > 0) {
            auto& entry = wb.m_drain.front();
            const size_t remaining = entry.m_msg.size() - wb.m_offset;
            if (sent >= remaining) {
                sent -= remaining;
                if (entry.m_callback)
                    entry.m_callback();
                wb.m_drain.pop_front();
                wb.m_offset = 0;
            } else {
                wb.m_offset += sent;
                sent = 0;
            }
        }
    }
}

////////////////////////////////////////////
//            ConnectionHandle            //
////////////////////////////////////////////

void ConnectionHandle::disconnect()
{
    m_readerThread.disconnect(m_fd);
}

bool ConnectionHandle::trySendInline(MsgPacket& msg)
{
    // For non-TLS: try non-blocking inline send from the caller's thread.
    // For TLS: always queue to reader (SSL objects aren't safe for concurrent read+write).
    if (!m_tls) {
        if (m_readerThread.trySend(m_fd, msg))
            return true;
    }
    return false;
}

void ConnectionHandle::send(MsgPacket&& msg)
{
    if (trySendInline(msg)) {
        if (msg.m_callback)
            msg.m_callback();
        return;
    }

    // fall back to reader's write queue
    m_readerThread.queueWrite(m_fd, std::move(msg));
}

void ConnectionHandle::queueWrite(MsgPacket&& msg)
{
    m_readerThread.queueWrite(m_fd, std::move(msg));
}

bool ConnectionHandle::isReady() const
{
    return m_network.isConnectionReady(m_fd);
}

////////////////////////////////////////////
//             NetworkHandler             //
////////////////////////////////////////////

void NetworkHandler::start()
{
    if (m_settings.getSessionType() == SessionType::INITIATOR) {
        LOG_INFO("Attempting to connect...");
        const bool ret = m_network.connect(m_settings, shared_from_this());
        if (ret) {
            LOG_INFO("Successful connection.");
        } else {
            LOG_DEBUG("Unable to connect, will retry after next interval.");
        }
    } else if (m_settings.getSessionType() == SessionType::ACCEPTOR) {
        if (!m_network.hasAcceptor(m_settings)) {
            LOG_INFO("Attempting to create acceptor...");
            const bool ret = m_network.addAcceptor(m_settings, shared_from_this());
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
    if (m_stopped.exchange(true, std::memory_order_acq_rel))
        return;

    // remove any existing connection
    disconnect();

    // shut down if we're an acceptor
    if (m_settings.getSessionType() == SessionType::ACCEPTOR)
        m_network.removeAcceptor(m_settings);
}

void NetworkHandler::setSocketSettings(int fd)
{
    set_sock_opt(fd, IPPROTO_TCP, TCP_NODELAY, m_settings.getBool(SessionSettings::ENABLE_TCP_NODELAY));
    set_sock_opt(fd, IPPROTO_TCP, TCP_QUICKACK, m_settings.getBool(SessionSettings::ENABLE_TCP_QUICKACK));
}

void NetworkHandler::processMessage(std::string msg)
{
    m_delegate->onNetworkMessage(std::move(msg));
}

void NetworkHandler::update()
{
    m_delegate->onNetworkUpdate();
}

void NetworkHandler::send(MsgPacket&& msg)
{
    // Extract callback before acquiring the lock: if trySendInline succeeds,
    // we fire the callback AFTER releasing m_mutex.  This avoids recursive
    // locking (sendLogout's callback calls terminate() → disconnect() →
    // lock(m_mutex)) and lets us use a cheaper std::mutex.
    SendCallback_T callback;
    {
        std::lock_guard lock(m_mutex);
        if (m_connection) {
            if (!m_valid.load(std::memory_order_acquire)) {
                m_connection = nullptr;
                return;
            }
            callback = std::move(msg.m_callback);
            msg.m_callback = {};
            if (m_connection->trySendInline(msg)) {
                // Inline send succeeded — callback fires below, outside lock
            } else {
                // Queued for async delivery — restore callback for writer thread
                msg.m_callback = std::move(callback);
                callback = {};
                m_connection->queueWrite(std::move(msg));
            }
        }
    }
    if (callback)
        callback();
}

void NetworkHandler::disconnect()
{
    std::lock_guard lock(m_mutex);
    m_connected.store(false, std::memory_order_release);
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
    m_connected.store(connection != nullptr, std::memory_order_release);
}

void NetworkHandler::invalidate()
{
    m_valid.store(false, std::memory_order_release);
    m_connected.store(false, std::memory_order_release);
}
