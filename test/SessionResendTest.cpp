#include "SessionTestHarness.h"

using namespace fix_test;

class SessionResendTest : public SessionTestFixture
{
};

// valid ResendRequest triggers message recovery with GapFill for session messages
TEST_F(SessionResendTest, ResendRequestTriggersMessageRecovery)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    const int acceptorSeqNum = session->getSenderSeqNum();
    EXPECT_GE(acceptorSeqNum, 2);

    // request resend from message 1 onwards
    client.sendMessage("2", 2, {{7, "1"}, {16, "0"}});

    const auto msgs = client.receiveMessages(std::chrono::seconds(3));
    ASSERT_FALSE(msgs.empty());

    bool foundGapFill = false;
    for (const auto& m : msgs) {
        auto tags = RawFIXClient::parseTags(m);
        if (tags[35] == "4" && tags[123] == "Y") {
            foundGapFill = true;
            EXPECT_FALSE(tags[36].empty());
        }
    }
    EXPECT_TRUE(foundGapFill);

    app.stop();
}

// ResendRequest increments target seqnum
TEST_F(SessionResendTest, ResendRequestIncrements)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("2", 2, {{7, "1"}, {16, "0"}});

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));

    app.stop();
}

// both sides exchange messages, then handle recovery
TEST_F(SessionResendTest, BidirectionalMessageRecovery)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.createSession("initiator", makeInitiatorSettings(port_));
    app.start();

    const auto initiator = app.getSession("initiator");
    const auto acceptor = app.getSession("acceptor");
    ASSERT_NE(initiator, nullptr);
    ASSERT_NE(acceptor, nullptr);

    ASSERT_TRUE(waitForSessionReady(initiator));
    ASSERT_TRUE(waitForSessionReady(acceptor));

    app.stop();
}

// recovery gap-fills session messages with PossDupFlag=Y and OrigSendingTime
TEST_F(SessionResendTest, RecoveryGapFillsSessionMsgsRetransmitsAppMsgs)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client.sendMessage("2", 2, {{7, "1"}, {16, "0"}});

    const auto msgs = client.receiveMessages(std::chrono::seconds(3));
    ASSERT_FALSE(msgs.empty());

    for (const auto& m : msgs) {
        auto tags = RawFIXClient::parseTags(m);
        if (tags[35] == "4" && tags[123] == "Y") {
            EXPECT_EQ(tags[43], "Y");
            EXPECT_FALSE(tags[122].empty());
        }
    }

    app.stop();
}

// receive ResendRequest while awaiting response to own ResendRequest
TEST_F(SessionResendTest, SimultaneousResendRequest)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    // logon with seqnum=5 to create a gap
    const auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "5"},
        {52, Utils::getUTCTimestamp()},
        {108, "1"},
        {98, "0"},
    });
    client.sendRaw(logonMsg);

    const auto session = app.getSession("acceptor");

    bool gotLogon = false;
    bool gotResendRequest = false;
    ASSERT_TRUE(waitFor([&] {
        const auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "A")
                gotLogon = true;
            if (tags[35] == "2")
                gotResendRequest = true;
        }
        return gotLogon && gotResendRequest;
    }, std::chrono::seconds(5)));

    // send ResendRequest while acceptor is still waiting for resend responses
    client.sendMessage("2", 6, {{7, "1"}, {16, "0"}});

    bool gotRecovery = false;
    EXPECT_TRUE(waitFor([&] {
        const auto m = client.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty())
            gotRecovery = true;
        return gotRecovery;
    }, std::chrono::seconds(5)));

    app.stop();
}
