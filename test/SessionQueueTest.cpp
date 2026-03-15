#include "SessionTestHarness.h"

using namespace fix_test;

class SessionQueueTest : public SessionTestFixture
{
};

// out-of-order message is queued; after gap fill, the queued message is processed
TEST_F(SessionQueueTest, QueuedMessageProcessedAfterGapFill)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // send msg 4, skipping 2 and 3 — queues msg 4, triggers ResendRequest
    client.sendMessage("0", 4);

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "2");  // ResendRequest
    EXPECT_EQ(tags[7], "2");   // BeginSeqNo

    // fill the gap: advance from 2 to 4
    auto gapFill = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "4"},
        {43, "Y"},
        {122, Utils::getUTCTimestamp()},
    });
    client.sendRaw(gapFill);

    // queued msg 4 should now be processed, target seqnum -> 5
    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 5; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// multiple out-of-order messages are queued and processed sequentially after gap fill
TEST_F(SessionQueueTest, MultipleQueuedMessagesProcessedInOrder)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // send msgs 5, 6, 7 (skipping 2-4) — all queued
    client.sendMessage("0", 5);
    client.sendMessage("0", 6);
    client.sendMessage("0", 7);

    auto response = client.receiveMessage();
    ASSERT_FALSE(response.empty());
    auto tags = RawFIXClient::parseTags(response);
    EXPECT_EQ(tags[35], "2");
    EXPECT_EQ(tags[7], "2");

    // fill the gap: advance from 2 to 5
    auto gapFill = buildRawMessage("FIX.4.2", {
        {35, "4"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "2"},
        {52, Utils::getUTCTimestamp()},
        {123, "Y"},
        {36, "5"},
        {43, "Y"},
        {122, Utils::getUTCTimestamp()},
    });
    client.sendRaw(gapFill);

    // all three queued messages (5, 6, 7) should be processed, target seqnum -> 8
    EXPECT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 8; }, std::chrono::seconds(3)));
    EXPECT_TRUE(session->getNetwork()->isConnected());

    app.stop();
}

// EndSeqNo=0 in ResendRequest means "resend all from BeginSeqNo through latest"
TEST_F(SessionQueueTest, ResendRequestEndSeqNoZeroMeansAll)
{
    Application app;
    app.createSession("acceptor", makeAcceptorSettings(port_));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));
    ASSERT_TRUE(client.performLogon("INITIATOR", "ACCEPTOR", 1, 30));

    auto session = app.getSession("acceptor");
    ASSERT_TRUE(waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    // request resend with EndSeqNo=0 (all subsequent)
    client.sendMessage("2", 2, {{7, "1"}, {16, "0"}});

    auto msgs = client.receiveMessages(std::chrono::seconds(3));
    ASSERT_FALSE(msgs.empty());

    // verify a GapFill was sent covering the logon ack
    bool foundGapFill = false;
    for (const auto& m : msgs) {
        auto t = RawFIXClient::parseTags(m);
        if (t[35] == "4" && t[123] == "Y") {
            foundGapFill = true;
            int newSeqNo = std::stoi(t[36]);
            EXPECT_GE(newSeqNo, 2);
        }
    }
    EXPECT_TRUE(foundGapFill);

    app.stop();
}
