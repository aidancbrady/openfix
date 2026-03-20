#pragma once

#include <FieldConvertors.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BenchmarkFixtures.h"

namespace perf {

// Path to QuickFIX's bundled FIX42 data dictionary.
// Searches common runfiles locations for external repo data.
inline std::string qfDictionaryPath()
{
    std::vector<std::string> candidates = {
        std::string(bench::kBenchmarkDictionaryPath),
        "FIXDictionary.xml",
        "test/FIXDictionary.xml",
    };
    for (const auto& c : candidates) {
        if (std::ifstream(c).good())
            return std::filesystem::absolute(c).string();
    }

    namespace fs = std::filesystem;
    for (auto& entry : fs::recursive_directory_iterator(".")) {
        if (entry.path().filename() == "FIXDictionary.xml")
            return fs::absolute(entry.path()).string();
    }
    throw std::runtime_error("Could not locate benchmark FIX dictionary");
}

// UTC timestamp in FIX format: YYYYMMDD-HH:MM:SS.sss
inline std::string qfTimestamp()
{
    FIX::UtcTimeStamp ts = FIX::UtcTimeStamp::now();
    return FIX::UtcTimeStampConvertor::convert(ts, 3);
}

// Build a raw FIX message string from tag-value pairs.
// Automatically computes BodyLength (tag 9) and CheckSum (tag 10).
inline std::string qfBuildRawMessage(
    const std::string& beginString,
    const std::vector<std::pair<int, std::string>>& fields)
{
    std::string body;
    for (const auto& [tag, val] : fields)
        body += std::to_string(tag) + "=" + val + '\x01';

    std::string prefix = "8=" + beginString + "\x01"
                         "9=" + std::to_string(body.size()) + "\x01";
    std::string full = prefix + body;

    int sum = 0;
    for (unsigned char c : full)
        sum += c;
    int cs = sum % 256;

    char csBuf[8];
    std::snprintf(csBuf, sizeof(csBuf), "%03d", cs);
    return full + "10=" + csBuf + "\x01";
}

// Allocate a dynamic port by binding to port 0.
inline int qfGetAvailablePort()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
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

// Poll-based wait for a condition.
inline bool qfWaitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

// Minimal raw TCP client for FIX protocol (same approach as openfix's RawFIXClient).
class QFRawClient
{
    int m_fd = -1;
    std::string m_recvBuf;

public:
    ~QFRawClient() { close(); }

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

        const int one = 1;
        ::setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        const int sndbuf = 1 << 20; // 1 MB
        ::setsockopt(m_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

        return true;
    }

    bool connectWithRetry(int port, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (connect(port))
                return true;
            close();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    int fd() const { return m_fd; }

    void sendRaw(const std::string& data)
    {
        if (m_fd >= 0)
            ::send(m_fd, data.data(), data.size(), MSG_NOSIGNAL);
    }

    std::string receiveMessage(std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            size_t searchStart = 0;
            while (true) {
                auto pos = m_recvBuf.find("\x01" "10=", searchStart);
                if (pos == std::string::npos || pos + 8 > m_recvBuf.size())
                    break;
                if (m_recvBuf[pos + 7] == '\x01') {
                    auto msg = m_recvBuf.substr(0, pos + 8);
                    m_recvBuf.erase(0, pos + 8);
                    return msg;
                }
                searchStart = pos + 1;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0)
                break;

            pollfd pfd {m_fd, POLLIN, 0};
            int ret = ::poll(&pfd, 1, std::min<long long>(remaining.count(), 10));
            if (ret > 0) {
                char buf[4096];
                ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                m_recvBuf.append(buf, n);
            }
        }
        return "";
    }

    void close()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_recvBuf.clear();
    }

    bool performLogon(const std::string& sender = "INITIATOR",
                      const std::string& target = "ACCEPTOR",
                      int seqNum = 1, int heartBtInt = 30,
                      const std::string& beginString = "FIX.4.2")
    {
        auto msg = qfBuildRawMessage(beginString, {
            {35, "A"},
            {49, sender},
            {56, target},
            {34, std::to_string(seqNum)},
            {52, qfTimestamp()},
            {108, std::to_string(heartBtInt)},
            {98, "0"},
        });
        sendRaw(msg);
        auto response = receiveMessage(std::chrono::seconds(3));
        if (response.empty() || response.find("35=A") == std::string::npos)
            std::cerr << "[QFRawClient] Logon response: "
                      << (response.empty() ? "<empty>" : response) << "\n";
        return !response.empty() && response.find("35=A") != std::string::npos;
    }

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
            {52, qfTimestamp()},
        };
        for (const auto& f : extraFields)
            fields.push_back(f);

        sendRaw(qfBuildRawMessage(beginString, fields));
    }

    // Parse raw FIX bytes into tag -> value map.
    static std::unordered_map<int, std::string> parseTags(const std::string& raw)
    {
        std::unordered_map<int, std::string> result;
        size_t pos = 0;
        while (pos < raw.size()) {
            auto eq = raw.find('=', pos);
            if (eq == std::string::npos)
                break;
            auto soh = raw.find('\x01', eq);
            if (soh == std::string::npos)
                soh = raw.size();
            try {
                int tag = std::stoi(raw.substr(pos, eq - pos));
                result[tag] = raw.substr(eq + 1, soh - eq - 1);
            } catch (...) {
            }
            pos = soh + 1;
        }
        return result;
    }
};

// Write a QuickFIX session config to a temporary file and return the path.
// The caller is responsible for cleaning up the file.
inline std::string qfWriteConfig(int port)
{
    const auto path = bench::quickfixConfigPath("session", port);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "[DEFAULT]\n"
        << "ConnectionType=acceptor\n"
        << "BeginString=" << bench::kBenchmarkBeginString << "\n"
        << "SenderCompID=" << bench::kAcceptorCompID << "\n"
        << "TargetCompID=" << bench::kInitiatorCompID << "\n"
        << "SocketAcceptPort=" << port << "\n"
        << "SocketNodelay=Y\n"
        << "StartTime=00:00:00\n"
        << "EndTime=00:00:00\n"
        << "NonStopSession=Y\n"
        << "HeartBtInt=" << bench::kHeartbeatIntervalSeconds << "\n"
        << "CheckLatency=N\n"
        << "UseDataDictionary=Y\n"
        << "DataDictionary=" << qfDictionaryPath() << "\n"
        << "FileStorePath=" << bench::quickfixStoreDir(port).string() << "\n"
        << "\n[SESSION]\n"
        << "BeginString=" << bench::kBenchmarkBeginString << "\n"
        << "SenderCompID=" << bench::kAcceptorCompID << "\n"
        << "TargetCompID=" << bench::kInitiatorCompID << "\n";
    out.close();
    return path.string();
}

} // namespace perf
