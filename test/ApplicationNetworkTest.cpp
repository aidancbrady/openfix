#include "SessionTestHarness.h"

namespace fix_test {

inline SessionSettings makeAcceptorSettings(int port, const std::string& sender, const std::string& target)
{
    auto s = makeAcceptorSettings(port);
    s.setString(SessionSettings::SENDER_COMP_ID, sender);
    s.setString(SessionSettings::TARGET_COMP_ID, target);
    return s;
}

inline SessionSettings makeInitiatorSettings(int port, const std::string& sender, const std::string& target)
{
    auto s = makeInitiatorSettings(port);
    s.setString(SessionSettings::SENDER_COMP_ID, sender);
    s.setString(SessionSettings::TARGET_COMP_ID, target);
    return s;
}

class ApplicationNetworkTest : public SessionTestFixture
{
protected:
    int port2_;

    void SetUp() override
    {
        SessionTestFixture::SetUp();
        port2_ = getAvailablePort();
    }
};

// ---- port sharing ----

// two acceptor sessions sharing the same port, both logon successfully
TEST_F(ApplicationNetworkTest, TwoAcceptorSessionsOnSamePort)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER1", "CLIENT1"));
    app.createSession("session2", makeAcceptorSettings(port_, "SERVER2", "CLIENT2"));
    app.start();

    RawFIXClient client1, client2;
    ASSERT_TRUE(client1.connectWithRetry(port_));
    ASSERT_TRUE(client1.performLogon("CLIENT1", "SERVER1"));

    ASSERT_TRUE(client2.connectWithRetry(port_));
    ASSERT_TRUE(client2.performLogon("CLIENT2", "SERVER2"));

    auto s1 = app.getSession("session1");
    auto s2 = app.getSession("session2");
    EXPECT_TRUE(waitFor([&] { return s1->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
    EXPECT_TRUE(waitFor([&] { return s2->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    app.stop();
}

// messages route to the correct session by CompID
TEST_F(ApplicationNetworkTest, AcceptorRoutesToCorrectSessionByCompID)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER1", "CLIENT1"));
    app.createSession("session2", makeAcceptorSettings(port_, "SERVER2", "CLIENT2"));
    app.start();

    RawFIXClient client1, client2;
    ASSERT_TRUE(client1.connectWithRetry(port_));
    ASSERT_TRUE(client1.performLogon("CLIENT1", "SERVER1"));

    ASSERT_TRUE(client2.connectWithRetry(port_));
    ASSERT_TRUE(client2.performLogon("CLIENT2", "SERVER2"));

    auto s1 = app.getSession("session1");
    auto s2 = app.getSession("session2");
    ASSERT_TRUE(waitFor([&] { return s1->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return s2->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client1.sendMessage("0", 2, {}, "CLIENT1", "SERVER1");
    EXPECT_TRUE(waitFor([&] { return s1->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));
    EXPECT_EQ(s2->getTargetSeqNum(), 2);

    app.stop();
}

// unknown CompIDs -> connection closed
TEST_F(ApplicationNetworkTest, UnknownCompIDConnectionRejected)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    auto msg = buildRawMessage("FIX.4.2", {
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

// already-connected session rejects duplicate connection
TEST_F(ApplicationNetworkTest, DuplicateConnectionToSameSessionRejected)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    app.start();

    RawFIXClient client1;
    ASSERT_TRUE(client1.connectWithRetry(port_));
    ASSERT_TRUE(client1.performLogon("CLIENT", "SERVER"));

    auto s = app.getSession("session1");
    ASSERT_TRUE(waitFor([&] { return s->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    RawFIXClient client2;
    ASSERT_TRUE(client2.connectWithRetry(port_));
    auto logonMsg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "CLIENT"},
        {56, "SERVER"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client2.sendRaw(logonMsg);

    EXPECT_TRUE(client2.waitForDisconnect(std::chrono::seconds(3)));
    EXPECT_TRUE(client1.isConnected());

    app.stop();
}

// logout of one session doesn't affect the other on the same port
TEST_F(ApplicationNetworkTest, SecondSessionUnaffectedByFirstSessionLogout)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER1", "CLIENT1"));
    app.createSession("session2", makeAcceptorSettings(port_, "SERVER2", "CLIENT2"));
    app.start();

    RawFIXClient client1, client2;
    ASSERT_TRUE(client1.connectWithRetry(port_));
    ASSERT_TRUE(client1.performLogon("CLIENT1", "SERVER1"));

    ASSERT_TRUE(client2.connectWithRetry(port_));
    ASSERT_TRUE(client2.performLogon("CLIENT2", "SERVER2"));

    auto s1 = app.getSession("session1");
    auto s2 = app.getSession("session2");
    ASSERT_TRUE(waitFor([&] { return s1->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return s2->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    client1.sendMessage("5", 2, {}, "CLIENT1", "SERVER1");
    auto response = client1.receiveMessage(std::chrono::seconds(3));
    EXPECT_FALSE(response.empty());

    client2.sendMessage("0", 2, {}, "CLIENT2", "SERVER2");
    EXPECT_TRUE(waitFor([&] { return s2->getTargetSeqNum() >= 3; }, std::chrono::seconds(3)));
    EXPECT_TRUE(client2.isConnected());

    app.stop();
}

// ---- mixed mode ----

// both acceptor and initiator sessions in the same application
TEST_F(ApplicationNetworkTest, AcceptorAndInitiatorInSameApplication)
{
    Application app1, app2;
    app1.createSession("acc", makeAcceptorSettings(port_, "A_ACC", "B_INIT"));
    app1.createSession("init", makeInitiatorSettings(port2_, "A_INIT", "B_ACC"));

    app2.createSession("acc", makeAcceptorSettings(port2_, "B_ACC", "A_INIT"));
    app2.createSession("init", makeInitiatorSettings(port_, "B_INIT", "A_ACC"));

    app1.start();
    app2.start();

    auto a1_acc = app1.getSession("acc");
    auto a1_init = app1.getSession("init");
    auto a2_acc = app2.getSession("acc");
    auto a2_init = app2.getSession("init");

    EXPECT_TRUE(waitFor([&] { return a1_acc->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return a2_acc->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return a1_init->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return a2_init->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));

    app2.stop();
    app1.stop();
}

// multiple initiator sessions connecting to separate acceptors
TEST_F(ApplicationNetworkTest, MultipleInitiatorSessions)
{
    Application acceptorApp;
    acceptorApp.createSession("acc1", makeAcceptorSettings(port_, "SERVER1", "CLIENT1"));
    acceptorApp.createSession("acc2", makeAcceptorSettings(port2_, "SERVER2", "CLIENT2"));
    acceptorApp.start();

    Application initiatorApp;
    initiatorApp.createSession("init1", makeInitiatorSettings(port_, "CLIENT1", "SERVER1"));
    initiatorApp.createSession("init2", makeInitiatorSettings(port2_, "CLIENT2", "SERVER2"));
    initiatorApp.start();

    auto acc1 = acceptorApp.getSession("acc1");
    auto acc2 = acceptorApp.getSession("acc2");
    auto init1 = initiatorApp.getSession("init1");
    auto init2 = initiatorApp.getSession("init2");

    EXPECT_TRUE(waitFor([&] { return acc1->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return acc2->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return init1->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    EXPECT_TRUE(waitFor([&] { return init2->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));

    initiatorApp.stop();
    acceptorApp.stop();
}

// initiator reconnects automatically after server restarts
TEST_F(ApplicationNetworkTest, InitiatorReconnectsAfterDisconnect)
{
    Application acceptorApp;
    acceptorApp.createSession("acc", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    acceptorApp.start();

    Application initiatorApp;
    initiatorApp.createSession("init", makeInitiatorSettings(port_, "CLIENT", "SERVER"));
    initiatorApp.start();

    auto acc = acceptorApp.getSession("acc");
    auto init = initiatorApp.getSession("init");

    ASSERT_TRUE(waitFor([&] { return acc->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));
    ASSERT_TRUE(waitFor([&] { return init->getTargetSeqNum() >= 2; }, std::chrono::seconds(5)));

    acceptorApp.stop();
    std::filesystem::remove_all("./data");

    Application acceptorApp2;
    acceptorApp2.createSession("acc", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    acceptorApp2.start();

    acc = acceptorApp2.getSession("acc");

    EXPECT_TRUE(waitFor([&] { return acc->getTargetSeqNum() >= 2; }, std::chrono::seconds(10)));

    initiatorApp.stop();
    acceptorApp2.stop();
}

// ---- lifecycle ----

// stopping the application disconnects all active sessions
TEST_F(ApplicationNetworkTest, StopApplicationDisconnectsAllSessions)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER1", "CLIENT1"));
    app.createSession("session2", makeAcceptorSettings(port_, "SERVER2", "CLIENT2"));
    app.start();

    RawFIXClient client1, client2;
    ASSERT_TRUE(client1.connectWithRetry(port_));
    ASSERT_TRUE(client1.performLogon("CLIENT1", "SERVER1"));

    ASSERT_TRUE(client2.connectWithRetry(port_));
    ASSERT_TRUE(client2.performLogon("CLIENT2", "SERVER2"));

    auto s1 = app.getSession("session1");
    auto s2 = app.getSession("session2");
    ASSERT_TRUE(waitFor([&] { return s1->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));
    ASSERT_TRUE(waitFor([&] { return s2->getTargetSeqNum() >= 2; }, std::chrono::seconds(3)));

    app.stop();

    EXPECT_TRUE(client1.waitForDisconnect(std::chrono::seconds(3)));
    EXPECT_TRUE(client2.waitForDisconnect(std::chrono::seconds(3)));
}

TEST_F(ApplicationNetworkTest, CreateSessionWithDuplicateNameThrows)
{
    Application app;
    app.createSession("mySession", makeAcceptorSettings(port_));
    EXPECT_THROW(app.createSession("mySession", makeAcceptorSettings(port2_)), std::runtime_error);
}

// ---- connection routing edge cases ----

// missing SenderCompID -> connection closed
TEST_F(ApplicationNetworkTest, MissingSenderCompIDClosesConnection)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {56, "SERVER"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

// missing TargetCompID -> connection closed
TEST_F(ApplicationNetworkTest, MissingTargetCompIDClosesConnection)
{
    Application app;
    app.createSession("session1", makeAcceptorSettings(port_, "SERVER", "CLIENT"));
    app.start();

    RawFIXClient client;
    ASSERT_TRUE(client.connectWithRetry(port_));

    auto msg = buildRawMessage("FIX.4.2", {
        {35, "A"},
        {49, "CLIENT"},
        {34, "1"},
        {52, Utils::getUTCTimestamp()},
        {108, "30"},
        {98, "0"},
    });
    client.sendRaw(msg);

    EXPECT_TRUE(client.waitForDisconnect(std::chrono::seconds(3)));

    app.stop();
}

}  // namespace fix_test
