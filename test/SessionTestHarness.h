#pragma once

#include <gtest/gtest.h>
#include <openfix/Application.h>
#include <openfix/Checksum.h>
#include <openfix/Utils.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace fix_test {

constexpr const char* kFixDictionaryPath = "test/FIXDictionary.xml";

// ---- Utilities ----

inline int getAvailablePort()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::runtime_error("Failed to create socket for dynamic port allocation");

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        throw std::runtime_error("Failed to bind dynamic test port");
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        throw std::runtime_error("Failed to resolve dynamic test port");
    }

    const int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

inline bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

inline SessionSettings makeAcceptorSettings(int port)
{
    SessionSettings settings;
    settings.setString(SessionSettings::SESSION_TYPE_STR, "acceptor");
    settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
    settings.setString(SessionSettings::SENDER_COMP_ID, "ACCEPTOR");
    settings.setString(SessionSettings::TARGET_COMP_ID, "INITIATOR");
    settings.setString(SessionSettings::FIX_DICTIONARY, kFixDictionaryPath);
    settings.setLong(SessionSettings::ACCEPT_PORT, port);
    settings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    settings.setLong(SessionSettings::LOGON_INTERVAL, 1);
    settings.setLong(SessionSettings::RECONNECT_INTERVAL, 1);
    settings.setBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM, false);
    return settings;
}

inline SessionSettings makeInitiatorSettings(int port)
{
    SessionSettings settings;
    settings.setString(SessionSettings::SESSION_TYPE_STR, "initiator");
    settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
    settings.setString(SessionSettings::SENDER_COMP_ID, "INITIATOR");
    settings.setString(SessionSettings::TARGET_COMP_ID, "ACCEPTOR");
    settings.setString(SessionSettings::FIX_DICTIONARY, kFixDictionaryPath);
    settings.setString(SessionSettings::CONNECT_HOST, "127.0.0.1");
    settings.setLong(SessionSettings::CONNECT_PORT, port);
    settings.setLong(SessionSettings::CONNECT_TIMEOUT, 250);
    settings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    settings.setLong(SessionSettings::LOGON_INTERVAL, 1);
    settings.setLong(SessionSettings::RECONNECT_INTERVAL, 1);
    settings.setBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM, false);
    return settings;
}

// ---- Raw FIX Message Builder ----

// Build a FIX message with auto-computed BodyLength and CheckSum.
// `fields` should contain all tags EXCEPT 8, 9, and 10.
inline std::string buildRawMessage(const std::string& beginString,
                                   const std::vector<std::pair<int, std::string>>& fields)
{
    std::string body;
    for (const auto& [tag, val] : fields)
        body += std::to_string(tag) + "=" + val + std::string(1, '\x01');

    const std::string prefix = "8=" + beginString + "\x01" "9=" + std::to_string(body.size()) + "\x01";
    const std::string full = prefix + body;
    const uint8_t checksum = computeChecksum(full);
    return full + "10=" + std::string(formatChecksum(checksum).view()) + "\x01";
}

// Build a FIX message with an explicit (possibly invalid) checksum.
inline std::string buildRawMessageBadChecksum(const std::string& beginString,
                                              const std::vector<std::pair<int, std::string>>& fields,
                                              const std::string& checksumOverride)
{
    std::string body;
    for (const auto& [tag, val] : fields)
        body += std::to_string(tag) + "=" + val + std::string(1, '\x01');

    const std::string prefix = "8=" + beginString + "\x01" "9=" + std::to_string(body.size()) + "\x01";
    return prefix + body + "10=" + checksumOverride + "\x01";
}

// Build a FIX message with an explicit (possibly invalid) BodyLength.
inline std::string buildRawMessageBadBodyLength(const std::string& beginString,
                                                const std::vector<std::pair<int, std::string>>& fields,
                                                int bodyLengthOverride)
{
    std::string body;
    for (const auto& [tag, val] : fields)
        body += std::to_string(tag) + "=" + val + std::string(1, '\x01');

    const std::string prefix = "8=" + beginString + "\x01" "9=" + std::to_string(bodyLengthOverride) + "\x01";
    const std::string full = prefix + body;
    const uint8_t checksum = computeChecksum(full);
    return full + "10=" + std::string(formatChecksum(checksum).view()) + "\x01";
}

// ---- Raw FIX Client ----

class RawFIXClient
{
    int m_fd = -1;
    std::string m_recvBuf;

public:
    ~RawFIXClient() { close(); }

    bool connect(int port)
    {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0)
            return false;

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
        return true;
    }

    // Connect with retry, waiting for the acceptor to start listening.
    bool connectWithRetry(int port, std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (connect(port))
                return true;
            close();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    void sendRaw(const std::string& data)
    {
        if (m_fd >= 0)
            ::send(m_fd, data.data(), data.size(), MSG_NOSIGNAL);
    }

    // Receive a single complete FIX message (8=...10=xxx\x01).
    std::string receiveMessage(std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            // Search for checksum trailer: \x0110=NNN\x01
            size_t searchStart = 0;
            while (true) {
                const auto pos = m_recvBuf.find("\x01" "10=", searchStart);
                if (pos == std::string::npos || pos + 8 > m_recvBuf.size())
                    break;
                if (m_recvBuf[pos + 7] == '\x01') {
                    const auto msg = m_recvBuf.substr(0, pos + 8);
                    m_recvBuf.erase(0, pos + 8);
                    return msg;
                }
                searchStart = pos + 1;
            }

            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0)
                break;

            pollfd pfd {m_fd, POLLIN, 0};
            const int ret = ::poll(&pfd, 1, std::min<long long>(remaining.count(), 10));
            if (ret > 0) {
                char buf[4096];
                const ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                m_recvBuf.append(buf, n);
            }
        }
        return "";
    }

    // Check if the TCP connection is still open.
    // Drains any pending data into m_recvBuf so that a server-side close
    // is detected even when unread messages are buffered.
    bool isConnected()
    {
        if (m_fd < 0)
            return false;
        // Drain any pending data.
        char buf[4096];
        while (true) {
            ssize_t n = ::recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0) {
                m_recvBuf.append(buf, n);
                continue;
            }
            if (n == 0)
                return false;  // peer closed
            // n < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;  // no more data, connection still open
            return false;  // error
        }
    }

    // Wait until the connection is closed by the peer.
    bool waitForDisconnect(std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        return waitFor([this] { return !isConnected(); }, timeout);
    }

    void close()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_recvBuf.clear();
    }

    // Perform logon handshake, returns true if logon ack received.
    bool performLogon(const std::string& sender = "INITIATOR",
                      const std::string& target = "ACCEPTOR",
                      int seqNum = 1, int heartBtInt = 30)
    {
        const auto msg = buildRawMessage("FIX.4.2", {
            {35, "A"},
            {49, sender},
            {56, target},
            {34, std::to_string(seqNum)},
            {52, Utils::getUTCTimestamp()},
            {108, std::to_string(heartBtInt)},
            {98, "0"},
        });
        sendRaw(msg);
        const auto response = receiveMessage(std::chrono::seconds(3));
        return !response.empty() && response.find("35=A") != std::string::npos;
    }

    // Send a well-formed message after logon.
    void sendMessage(const std::string& msgType, int seqNum,
                     const std::vector<std::pair<int, std::string>>& extraFields = {},
                     const std::string& sender = "INITIATOR",
                     const std::string& target = "ACCEPTOR",
                     const std::string& beginString = "FIX.4.2")
    {
        std::vector<std::pair<int, std::string>> fields = {
            {35, msgType},
            {49, sender},
            {56, target},
            {34, std::to_string(seqNum)},
            {52, Utils::getUTCTimestamp()},
        };
        for (const auto& f : extraFields)
            fields.push_back(f);

        sendRaw(buildRawMessage(beginString, fields));
    }

    // Parse raw FIX message bytes into tag-value map.
    static HashMapT<int, std::string> parseTags(const std::string& raw)
    {
        HashMapT<int, std::string> result;
        size_t pos = 0;
        while (pos < raw.size()) {
            const auto eq = raw.find('=', pos);
            if (eq == std::string::npos)
                break;
            auto soh = raw.find('\x01', eq);
            if (soh == std::string::npos)
                soh = raw.size();
            try {
                const int tag = std::stoi(raw.substr(pos, eq - pos));
                result[tag] = raw.substr(eq + 1, soh - eq - 1);
            } catch (...) {
            }
            pos = soh + 1;
        }
        return result;
    }

    // Receive all complete messages. Waits up to `timeout` for the first
    // message, then drains any further messages using a short follow-up
    // timeout so we don't block for the full window after the last message.
    std::vector<std::string> receiveMessages(std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        std::vector<std::string> messages;
        auto msg = receiveMessage(timeout);
        while (!msg.empty()) {
            messages.push_back(std::move(msg));
            msg = receiveMessage(std::chrono::milliseconds(200));
        }
        return messages;
    }
};

// ---- Base Test Fixture ----

class SessionTestFixture : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        PlatformSettings::load({
            {"AdminWebsitePort", "0"},
            {"UpdateDelay", "10"},
            {"InputThreads", "1"},
            {"WriterThreads", "1"},
            {"EpollTimeout", "10"},
        });
    }

    int port_;

    void SetUp() override
    {
        port_ = getAvailablePort();
        // Clean up persistent store data from previous tests.
        std::filesystem::remove_all("./data");
    }

    void TearDown() override
    {
        // Ensure store data doesn't leak into the next test.
        std::filesystem::remove_all("./data");
    }
};

}  // namespace fix_test
