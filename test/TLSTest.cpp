#include <gtest/gtest.h>
#include <openfix/Application.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace
{
constexpr const char* kFixDictionaryPath = "test/FIXDictionary.xml";
constexpr const char* kCAPath = "test/tls/ca.crt";
constexpr const char* kServerCertPath = "test/tls/server.crt";
constexpr const char* kServerKeyPath = "test/tls/server.key";
constexpr const char* kClientCertPath = "test/tls/client.crt";
constexpr const char* kClientKeyPath = "test/tls/client.key";

int getAvailablePort()
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

bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return condition();
}

SessionSettings makeAcceptorSettings(int port)
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
    return settings;
}

SessionSettings makeInitiatorSettings(int port)
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
    return settings;
}
}  // namespace

class TLSTest : public ::testing::Test
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
};

TEST_F(TLSTest, ConnectsWithTrustedServerCertificate)
{
    const int port = getAvailablePort();

    Application app;
    auto acceptor = makeAcceptorSettings(port);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, false);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    initiator.setString(SessionSettings::TLS_CA_FILE, kCAPath);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_TRUE(waitFor([&]() { return initiatorSession->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    app.stop();
}

TEST_F(TLSTest, FailsWhenServerCertificateIsUntrusted)
{
    const int port = getAvailablePort();

    Application app;
    auto acceptor = makeAcceptorSettings(port);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, false);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_FALSE(waitFor([&]() { return initiatorSession->getNetwork()->isConnected(); }, std::chrono::seconds(2)));

    app.stop();
}

TEST_F(TLSTest, FailsWhenClientCertificateIsRequiredButMissing)
{
    const int port = getAvailablePort();

    Application app;
    auto acceptor = makeAcceptorSettings(port);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    acceptor.setBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT, true);
    acceptor.setString(SessionSettings::TLS_CA_FILE, kCAPath);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    initiator.setString(SessionSettings::TLS_CA_FILE, kCAPath);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_FALSE(waitFor([&]() { return initiatorSession->getNetwork()->isConnected(); }, std::chrono::seconds(2)));

    app.stop();
}

TEST_F(TLSTest, ConnectsWithMutualTLSWhenClientCertificateProvided)
{
    const int port = getAvailablePort();

    Application app;
    auto acceptor = makeAcceptorSettings(port);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    acceptor.setBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT, true);
    acceptor.setString(SessionSettings::TLS_CA_FILE, kCAPath);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    initiator.setString(SessionSettings::TLS_CA_FILE, kCAPath);
    initiator.setString(SessionSettings::TLS_CERT_FILE, kClientCertPath);
    initiator.setString(SessionSettings::TLS_KEY_FILE, kClientKeyPath);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_TRUE(waitFor([&]() { return initiatorSession->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    app.stop();
}
