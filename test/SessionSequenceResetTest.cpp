#include "SessionTestHarness.h"

using namespace fix_test;

class SessionSequenceResetTest : public SessionTestFixture
{
};

// ---- gap fill mode ----

// GapFill NewSeqNo > MsgSeqNum, MsgSeqNum == NextNumIn -> advance to NewSeqNo
TEST_F(SessionSequenceResetTest, GapFillSetsNextExpected)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // GapFill: MsgSeqNum=2, NewSeqNo=5
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "5"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 5; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// GapFill NewSeqNo <= MsgSeqNum -> reject
TEST_F(SessionSequenceResetTest, GapFillAttemptToLowerTriggersReject)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // GapFill: MsgSeqNum=2, NewSeqNo=1 (attempt to lower)
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "1"},
    });
    client.sendRaw(msg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "3");   // reject
    EXPECT_EQ(tags[373], "5");  // IncorrectValueForTag

    app.stop();
}

// GapFill MsgSeqNum > NextNumIn -> queue and issue ResendRequest
TEST_F(SessionSequenceResetTest, GapFillWithHighSeqNumTriggersResendRequest)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // GapFill with MsgSeqNum=5 (expected 2) -> gap
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "5"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "10"},
    });
    client.sendRaw(msg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "2");  // ResendRequest
    EXPECT_EQ(tags[7], "2");   // BeginSeqNo

    app.stop();
}

// ---- reset mode ----

// Reset NewSeqNo > NextNumIn -> accept
TEST_F(SessionSequenceResetTest, ResetSetsNextExpected)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // Reset: NewSeqNo=10
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {36, "10"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 10; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// Reset NewSeqNo < targetSeqNum -> logout
TEST_F(SessionSequenceResetTest, ResetToLowerTriggersLogout)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // advance target to 20 via valid reset
    const auto msg1 = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {36, "20"},
    });
    client.sendRaw(msg1);
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 20; }, std::chrono::seconds(3)));

    // advance target to 50
    const auto msg1b = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "20"},
        {52, Utils::getUTCTimestamp()},
        {36, "50"},
    });
    client.sendRaw(msg1b);
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 50; }, std::chrono::seconds(3)));

    // reset with NewSeqNo=40 < targetSeqNum=50 -> logout
    const auto msg2 = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "30"},
        {52, Utils::getUTCTimestamp()},
        {36, "40"},
    });
    client.sendRaw(msg2);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "5");  // logout

    app.stop();
}

// reset mode is exempt from normal seqnum validation — low MsgSeqNum still works
TEST_F(SessionSequenceResetTest, ResetAcceptedWithLowMsgSeqNum)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // Reset with MsgSeqNum=1 (below expected 2) and NewSeqNo=10
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {36, "10"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 10; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// GapFill NewSeqNo == MsgSeqNum (no advance) -> reject
TEST_F(SessionSequenceResetTest, GapFillNoAdvanceTriggersReject)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    const auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // GapFill where NewSeqNo == MsgSeqNum (no advance)
    const auto msg = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "2"},
    });
    client.sendRaw(msg);

    const auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "3");   // reject
    EXPECT_EQ(tags[373], "5");  // IncorrectValueForTag

    app.stop();
}
