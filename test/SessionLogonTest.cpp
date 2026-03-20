#include "SessionTestHarness.h"

using namespace fix_test;

class SessionLogonTest : public SessionTestFixture
{
};

TEST_F(SessionLogonTest, SuccessfulLogon)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.createSession("initiator", makeInitiatorSettings(port_));
    app.start();

    const auto initiator = app.getSession("initiator");
    const auto acceptor = app.getSession("acceptor");
    ASSERT_NE(initiator, nullptr);
    ASSERT_NE(acceptor, nullptr);

    EXPECT_TRUE(waitForSessionReady(initiator));
    EXPECT_TRUE(waitForSessionReady(acceptor));

    app.stop();
}

TEST_F(SessionLogonTest, AcceptorRespondsWithLogonAck)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    const auto session = app.getSession("acceptor");
    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    app.stop();
}

// first message is not a logon -> disconnect
TEST_F(SessionLogonTest, NonLogonFirstMessageDisconnects)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    client.sendMessage("0", 1);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(5)));

    app.stop();
}

// unknown counterparty -> disconnect without FIX message
TEST_F(SessionLogonTest, UnknownCounterpartyDisconnects)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "UNKNOWN_SENDER"},
        {56, "UNKNOWN_TARGET"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

TEST_F(SessionLogonTest, ResetSeqNumFlagAccepted)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::RESET_SEQ_NUM_ON_LOGON, true);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {141, "Y"},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "A");  // logon ack

    app.stop();
}

TEST_F(SessionLogonTest, ResetSeqNumFlagRejectedWhenNotConfigured)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::RESET_SEQ_NUM_ON_LOGON, false);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {141, "Y"},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout

    app.stop();
}

// TestMessageIndicator mismatch causes logout
TEST_F(SessionLogonTest, TestMessageIndicatorMismatch)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::IS_TEST, false);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    // send logon with TestMessageIndicator=Y to a non-test session
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {464, "Y"},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

// MsgSeqNum higher than expected triggers ResendRequest
TEST_F(SessionLogonTest, LogonSeqNumTooHighTriggersResendRequest)
{
    auto settings = makeAcceptorSettings(port_);
    settings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);

    Application app;
    app.createSession("acceptor", settings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    // send logon with MsgSeqNum=5, acceptor expects 1
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "5"},
        {52, Utils::getUTCTimestamp()},
        {108, "1"},
        {98, "0"},
    });
    client.sendRaw(msg);

    const auto session = app.getSession("acceptor");

    bool foundLogon = false;
    bool foundResendRequest = false;
    EXPECT_TRUE(waitFor([&] {
        const auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "A")
                foundLogon = true;
            if (tags[35] == "2") {
                foundResendRequest = true;
                EXPECT_EQ(tags[7], "1");  // BeginSeqNo=1
            }
        }
        return foundLogon && foundResendRequest;
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(foundLogon);
    EXPECT_TRUE(foundResendRequest);

    app.stop();
}

// MsgSeqNum too low on logon triggers logout
TEST_F(SessionLogonTest, LogonSeqNumTooLowTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    // logon so acceptor's target seqnum advances to 2
    {
        RawFIXClient client;
        ASSERT_TRUE(client.connectWithRetry(port_));
        ASSERT_TRUE(client.performLogon());
    }

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return !session->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    // reconnect with MsgSeqNum=1 (acceptor expects 2)
    RawFIXClient client2;
    ASSERT_TRUE(client2.connectWithRetry(port_));

    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client2.sendRaw(msg);

    const auto response = client2.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout
    EXPECT_TRUE(tags[58].find("too low") != std::string::npos);

    app.stop();
}
