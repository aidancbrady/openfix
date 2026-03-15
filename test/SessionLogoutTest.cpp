#include "SessionTestHarness.h"

using namespace fix_test;

class SessionLogoutTest : public SessionTestFixture
{
};

// initiate logout -> send Logout, wait for ack, disconnect
TEST_F(SessionLogoutTest, InitiateLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.createSession("initiator", makeInitiatorSettings(port_));
    app.start();

    auto initiator = app.getSession("initiator");
    auto acceptor = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return initiator->getNetwork()->isConnected(); }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return acceptor->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    initiator->stop();

    EXPECT_TRUE(waitFor([&] { return !initiator->getNetwork()->isConnected(); }, std::chrono::seconds(3)));
    EXPECT_TRUE(waitFor([&] { return !acceptor->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    app.stop();
}

// receive unsolicited Logout -> send ack, disconnect
TEST_F(SessionLogoutTest, ReceiveUnsolicitedLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("5", 2, {{58, "Client shutting down"}});

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout ack

    EXPECT_TRUE(waitFor([&] { return !session->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    app.stop();
}

// Logout ack not received within timeout -> disconnect anyway
TEST_F(SessionLogoutTest, LogoutAckTimeout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return session->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    client.sendMessage("0", 2);
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(10)));

    app.stop();
}

// logout includes Text(58) with descriptive reason
TEST_F(SessionLogoutTest, LogoutContainsTextReason)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // trigger logout via wrong BeginString
    client.sendMessage("0", 2, {}, "INITIATOR", "ACCEPTOR", "FIX.4.3");

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");
    EXPECT_FALSE(tags[58].empty());
    EXPECT_TRUE(tags[58].find("BeginString") != std::string::npos);

    app.stop();
}

// Logout received while in TEST_REQUEST state -> ack and disconnect
TEST_F(SessionLogoutTest, LogoutDuringTestRequestState)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    acceptorSettings.setDouble(SessionSettings::TEST_REQUEST_THRESHOLD, 2.0);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // wait for TestRequest (acceptor enters TEST_REQUEST state after 2s inactivity)
    std::string testReqID;
    ASSERT_TRUE(waitFor([&] {
        auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "1")
                testReqID = tags[112];
        }
        return !testReqID.empty();
    }, std::chrono::seconds(5)));

    // send Logout while acceptor is in TEST_REQUEST state
    int nextSeq = session->getTargetSeqNum();
    client.sendMessage("5", nextSeq, {{58, "Client shutting down"}});

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(5)));

    app.stop();
}

// receive Logout ack after initiating Logout -> clean disconnect
TEST_F(SessionLogoutTest, LogoutAckReceivedCleanDisconnect)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.createSession("initiator", makeInitiatorSettings(port_));
    app.start();

    auto initiator = app.getSession("initiator");
    auto acceptor = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return initiator->getNetwork()->isConnected(); }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return acceptor->getNetwork()->isConnected(); }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return initiator->getSenderSeqNum() >= 2; }, std::chrono::seconds(3)));

    acceptor->stop();

    EXPECT_TRUE(waitFor([&] { return !acceptor->getNetwork()->isConnected(); }, std::chrono::seconds(3)));
    EXPECT_TRUE(waitFor([&] { return !initiator->getNetwork()->isConnected(); }, std::chrono::seconds(5)));

    app.stop();
}
