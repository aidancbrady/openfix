#include "SessionTestHarness.h"

using namespace fix_test;

class SessionPersistenceTest : public SessionTestFixture
{
};

// sequence numbers persist across restart
TEST_F(SessionPersistenceTest, SequenceNumbersPersistAcrossRestart)
{
    // phase 1: establish session, exchange messages, then stop
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
        client.sendMessage("0", 3);
        ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 4; }, std::chrono::seconds(3)));
        EXPECT_GE(session->getSenderSeqNum(), 2);

        app.stop();
    }

    // phase 2: restart with same persistent store, verify persisted state
    {
        Application app;
        app.createSession("acceptor", makeAcceptorSettings(port_));
        app.start();

        const auto session = app.getSession("acceptor");
        EXPECT_GE(session->getTargetSeqNum(), 4);
        EXPECT_GE(session->getSenderSeqNum(), 2);

        app.stop();
    }
}

// cached messages are available for resend after restart
TEST_F(SessionPersistenceTest, CachedMessagesAvailableAfterRestart)
{
    int acceptorSenderSeqNum = 0;

    // phase 1: establish session so acceptor caches its logon ack
    {
        Application app;
        app.createSession("acceptor", makeAcceptorSettings(port_));
        app.start();

        RawFIXClient client;
        ASSERT_TRUE(client.connectWithRetry(port_));
        ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

        const auto session = app.getSession("acceptor");
        ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
        acceptorSenderSeqNum = session->getSenderSeqNum();
        EXPECT_GE(acceptorSenderSeqNum, 2);

        app.stop();
    }

    // phase 2: restart, logon with correct seqnums, request resend
    {
        Application app;
        app.createSession("acceptor", makeAcceptorSettings(port_));
        app.start();

        RawFIXClient client;
        ASSERT_TRUE(client.connectWithRetry(port_));
        ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 2, 30));

        const auto session = app.getSession("acceptor");
        ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));

        // request resend of message 1 (the original logon ack from phase 1)
        client.sendMessage("2", 3, {{7, "1"}, {16, "0"}});

        const auto msgs = client.receiveMessages(std::chrono::seconds(3));
        ASSERT_FALSE(msgs.empty());

        bool gotRecovery = false;
        for (const auto& m : msgs) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "4")
                gotRecovery = true;
        }
        EXPECT_TRUE(gotRecovery);

        app.stop();
    }
}

// NextExpectedMsgSeqNum < sender seqnum triggers message recovery on logon
TEST_F(SessionPersistenceTest, NextExpectedMsgSeqNumTriggersRecovery)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM, true);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    const auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(logonMsg);

    const auto session = app.getSession("acceptor");

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto logonTags = RawFIXClient::parseTags(response);
    EXPECT_EQ(logonTags[35], "A");
    EXPECT_FALSE(logonTags[789].empty());  // NextExpectedMsgSeqNum present

    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // disconnect and reconnect
    client.close();
    ASSERT_TRUE(waitFor([&] { return !session->getNetwork()->isConnected(); }, std::chrono::seconds(3)));

    RawFIXClient client2;
    ASSERT_TRUE(client2.connectWithRetry(port_));

    // logon with NextExpectedMsgSeqNum=1 (behind acceptor's sender seqnum)
    const auto logon2 = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
        {789, "1"},
    });
    client2.sendRaw(logon2);

    bool gotLogon = false;
    bool gotRecovery = false;
    EXPECT_TRUE(waitFor([&] {
        const auto m = client2.receiveMessage(std::chrono::milliseconds(200));
        if (!m.empty()) {
            auto tags = RawFIXClient::parseTags(m);
            if (tags[35] == "A")
                gotLogon = true;
            if (tags[35] == "4")
                gotRecovery = true;
        }
        return gotLogon && gotRecovery;
    }, std::chrono::seconds(5)));
    EXPECT_TRUE(gotLogon);
    EXPECT_TRUE(gotRecovery);

    app.stop();
}

// NextExpectedMsgSeqNum == sender seqnum is a no-op (no recovery needed)
TEST_F(SessionPersistenceTest, NextExpectedMsgSeqNumEqualsNoRecovery)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM, true);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    // NextExpectedMsgSeqNum=1 matches acceptor's sender seqnum
    const auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
        {789, "1"},
    });
    client.sendRaw(logonMsg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "A");

    // no recovery messages should follow
    const auto extra = client.receiveMessage(std::chrono::milliseconds(200));
    if (!extra.empty()) {
        auto extraTags = RawFIXClient::parseTags(extra);
        EXPECT_NE(extraTags[35], "4");
    }

    app.stop();
}

// NextExpectedMsgSeqNum > sender seqnum triggers logout
TEST_F(SessionPersistenceTest, NextExpectedMsgSeqNumTooHighTriggersLogout)
{
    auto acceptorSettings = makeAcceptorSettings(port_);
    acceptorSettings.setBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM, true);

    Application app;
    app.createSession("acceptor", acceptorSettings);
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    // NextExpectedMsgSeqNum=100 (way higher than acceptor's sender seqnum of 1)
    const auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
        {789, "100"},
    });
    client.sendRaw(logonMsg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout
    EXPECT_TRUE(tags[58].find("too high") != std::string::npos);

    app.stop();
}
