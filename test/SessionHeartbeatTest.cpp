#include "SessionTestHarness.h"

using namespace fix_test;

class SessionHeartbeatTest : public SessionTestFixture
{
};

// no data sent during HeartBtInt -> send Heartbeat
TEST_F(SessionHeartbeatTest, HeartbeatSentOnInactivity)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    const int seqAfterLogon = session->getSenderSeqNum();
    EXPECT_TRUE(waitFor([&] { return session->getSenderSeqNum() > seqAfterLogon; }, std::chrono::seconds(3)));

    const auto response = client.receiveMessage(std::chrono::seconds(3));
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "0");  // heartbeat

    app.stop();
}

// TestRequest received -> respond with Heartbeat containing matching TestReqID
TEST_F(SessionHeartbeatTest, HeartbeatResponseToTestRequest)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("1", 2, {{112, "TEST123"}});

    const auto response = client.receiveMessage(std::chrono::seconds(3));
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "0");
    EXPECT_EQ(tags[112], "TEST123");

    app.stop();
}

TEST_F(SessionHeartbeatTest, ValidHeartbeatAccepted)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("0", 2);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// no data received during HeartBtInt + threshold -> send TestRequest
TEST_F(SessionHeartbeatTest, TestRequestSentOnTimeout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    bool foundTestRequest = false;
    EXPECT_TRUE(waitFor([&] {
        const auto m = client.receiveMessage(std::chrono::milliseconds(100));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "1") {
                foundTestRequest = true;
                EXPECT_FALSE(tags[112].empty());
            }
        }
        return foundTestRequest;
    }, std::chrono::seconds(3)));
    EXPECT_TRUE(foundTestRequest);

    app.stop();
}

// no response to TestRequest within timeout -> disconnect
TEST_F(SessionHeartbeatTest, DisconnectOnTestRequestTimeout)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    acceptorSettings.setDouble(SessionSettings::TEST_REQUEST_THRESHOLD, 1.0);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(5)));

    app.stop();
}

// heartbeat with wrong TestReqID does not resolve TEST_REQUEST state
TEST_F(SessionHeartbeatTest, WrongTestReqIDDoesNotResolveTestRequest)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    acceptorSettings.setDouble(SessionSettings::TEST_REQUEST_THRESHOLD, 1.0);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // wait for the acceptor to send a TestRequest
    std::string testReqID;
    ASSERT_TRUE(waitFor([&] {
        const auto m = client.receiveMessage(std::chrono::milliseconds(100));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "1") {
                testReqID = tags[112];
                return true;
            }
        }
        return false;
    }, std::chrono::seconds(3)));
    ASSERT_FALSE(testReqID.empty());

    // respond with wrong TestReqID
    const int nextSeq = session->getTargetSeqNum();
    client.sendMessage("0", nextSeq, {{112, "WRONG_ID"}});

    // should eventually disconnect since correct TestReqID was never answered
    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(5)));

    app.stop();
}

// any received message resets the heartbeat receive timer, not just heartbeats
TEST_F(SessionHeartbeatTest, AnyMessageResetsReceiveTimer)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setLong(SessionSettings::HEARTBEAT_INTERVAL, 1);
    acceptorSettings.setDouble(SessionSettings::TEST_REQUEST_THRESHOLD, 1.5);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // send reject messages every 1s — should prevent TestRequest from firing
    // (TestRequest fires after HBI * threshold = 1 * 1.5 = 1.5s of inactivity)
    int seq = 2;
    for (int i = 0; i < 3; ++i) {
        client.receiveMessage(std::chrono::milliseconds(100));
        client.sendMessage("3", seq++, {{45, "0"}, {373, "0"}});
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}
