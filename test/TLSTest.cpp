#include "SessionTestHarness.h"

using namespace fix_test;

namespace
{
constexpr const char* kCAPath = "test/tls/ca.crt";
constexpr const char* kServerCertPath = "test/tls/server.crt";
constexpr const char* kServerKeyPath = "test/tls/server.key";
constexpr const char* kClientCertPath = "test/tls/client.crt";
constexpr const char* kClientKeyPath = "test/tls/client.key";
}  // namespace

class TLSTest : public SessionTestFixture
{
};

TEST_F(TLSTest, ConnectsWithTrustedServerCertificate)
{
    Application app;
    auto acceptor = makeAcceptorSettings(port_);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, false);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port_);
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

// TLS handshake fails -> no logon ack received, target seqnum stays at 1
TEST_F(TLSTest, FailsWhenServerCertificateIsUntrusted)
{
    Application app;
    auto acceptor = makeAcceptorSettings(port_);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, false);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port_);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_FALSE(waitFor([&]() { return initiatorSession->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    app.stop();
}

// acceptor requires client cert but none provided -> no logon ack
TEST_F(TLSTest, FailsWhenClientCertificateIsRequiredButMissing)
{
    Application app;
    auto acceptor = makeAcceptorSettings(port_);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    acceptor.setBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT, true);
    acceptor.setString(SessionSettings::TLS_CA_FILE, kCAPath);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port_);
    initiator.setBool(SessionSettings::TLS_ENABLED, true);
    initiator.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    initiator.setString(SessionSettings::TLS_CA_FILE, kCAPath);

    app.createSession("acceptor", acceptor);
    app.createSession("initiator", initiator);
    app.start();

    auto initiatorSession = app.getSession("initiator");
    ASSERT_NE(initiatorSession, nullptr);
    EXPECT_FALSE(waitFor([&]() { return initiatorSession->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    app.stop();
}

TEST_F(TLSTest, ConnectsWithMutualTLSWhenClientCertificateProvided)
{
    Application app;
    auto acceptor = makeAcceptorSettings(port_);
    acceptor.setBool(SessionSettings::TLS_ENABLED, true);
    acceptor.setBool(SessionSettings::TLS_VERIFY_PEER, true);
    acceptor.setBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT, true);
    acceptor.setString(SessionSettings::TLS_CA_FILE, kCAPath);
    acceptor.setString(SessionSettings::TLS_CERT_FILE, kServerCertPath);
    acceptor.setString(SessionSettings::TLS_KEY_FILE, kServerKeyPath);

    auto initiator = makeInitiatorSettings(port_);
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
