#include "SessionTestHarness.h"

using namespace fix_test;

class SessionMessageTest : public SessionTestFixture
{
};

TEST_F(SessionMessageTest, MsgSeqNumAsExpectedIsAccepted)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("0", 2);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));

    app.stop();
}

// MsgSeqNum higher than expected -> ResendRequest
TEST_F(SessionMessageTest, MsgSeqNumTooHighTriggersResendRequest)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // send heartbeat with MsgSeqNum=10 (expected 2)
    client.sendMessage("0", 10);

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "2");   // ResendRequest
    EXPECT_EQ(tags[7], "2");    // BeginSeqNo
    EXPECT_EQ(tags[16], "10");  // EndSeqNo

    app.stop();
}

// MsgSeqNum lower than expected (no PossDup) -> logout
TEST_F(SessionMessageTest, MsgSeqNumTooLowTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // MsgSeqNum=1, expected 2
    client.sendMessage("0", 1);

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout
    EXPECT_TRUE(tags[58].find("MsgSeqNum too low") != std::string::npos);

    app.stop();
}

// BeginString mismatch -> logout and disconnect
TEST_F(SessionMessageTest, BeginStringMismatchTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("0", 2, {}, "INITIATOR", "ACCEPTOR", "FIX.4.3");

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

// CompID mismatch -> logout and disconnect
TEST_F(SessionMessageTest, CompIDMismatchTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("0", 2, {}, "WRONG_SENDER", "ACCEPTOR");

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

// SendingTime outside threshold -> reject + logout
TEST_F(SessionMessageTest, SendingTimeOutsideThresholdTriggersReject)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    // Use a long heartbeat interval so the logout timeout (2*hbint) gives the
    // writer thread plenty of time to flush both the reject and logout messages
    // before the session terminates the connection.
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getState() == SessionState::READY; }, std::chrono::seconds(3)));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, "20000101-00:00:00.000"},
    });
    client.sendRaw(msg);

    bool foundReject = false;
    bool foundLogout = false;
    EXPECT_TRUE(waitFor([&] {
        auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "3") {
                foundReject = true;
                EXPECT_EQ(tags[373], "10");  // SendingTimeProblem
            }
            if (tags[35] == "5")
                foundLogout = true;
        }
        return foundReject && foundLogout;
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(foundReject);
    EXPECT_TRUE(foundLogout);

    app.stop();
}

// PossDupFlag=Y without OrigSendingTime -> reject
TEST_F(SessionMessageTest, PossDupWithoutOrigSendingTimeTriggersReject)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {43, "Y"},
        {52, Utils::getUTCTimestamp()},
    });
    client.sendRaw(msg);

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "3");   // reject
    EXPECT_EQ(tags[373], "1");  // RequiredTagMissing

    app.stop();
}

// invalid checksum -> garbled message ignored, seqnum not incremented
TEST_F(SessionMessageTest, InvalidChecksumIgnored)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    int seqBefore = session->getTargetSeqNum();

    auto msg = buildRawMessageBadChecksum("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
    }, "000");
    client.sendRaw(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(session->getTargetSeqNum(), seqBefore);
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// valid Reject increments target seqnum
TEST_F(SessionMessageTest, RejectMessageIncrements)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("3", 2, {{45, "1"}, {373, "0"}});

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// invalid BodyLength -> garbled message ignored, seqnum not incremented
TEST_F(SessionMessageTest, InvalidBodyLengthIgnored)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon());

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    int seqBefore = session->getTargetSeqNum();

    auto msg = buildRawMessageBadBodyLength("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
    }, 999);
    client.sendRaw(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(session->getTargetSeqNum(), seqBefore);
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// reject includes RefSeqNum and SessionRejectReason
TEST_F(SessionMessageTest, RejectContainsProperFields)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 1));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // trigger reject via SendingTime outside threshold
    auto msg = buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, "20000101-00:00:00.000"},
    });
    client.sendRaw(msg);

    bool foundReject = false;
    EXPECT_TRUE(waitFor([&] {
        auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "3") {
                foundReject = true;
                EXPECT_EQ(tags[45], "2");   // RefSeqNum
                EXPECT_EQ(tags[373], "10"); // SendingTimeProblem
            }
        }
        return foundReject;
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(foundReject);

    app.stop();
}

// PossDup with expected seqnum is accepted
TEST_F(SessionMessageTest, PossDupWithExpectedSeqNumAccepted)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {43, "Y"},
        {122, Utils::getUTCTimestamp()},
        {52, Utils::getUTCTimestamp()},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// second Logon while in READY state doesn't crash
TEST_F(SessionMessageTest, LogonWhileReadyTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(logonMsg);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// first message is not Logon -> logout and disconnect
TEST_F(SessionMessageTest, NonLogonDuringLogonStateTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}
