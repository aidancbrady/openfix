#pragma once

#include "Config.h"
#include "Message.h"
#include "Network.h"
#include "Dictionary.h"
#include "FIXCache.h"
#include "FIXStore.h"
#include "FIXLogger.h"
#include "Fields.h"

#include <openfix/Log.h>

#include <memory>
#include <atomic>
#include <functional>

enum class SessionState
{
    LOGON,
    READY,
    TEST_REQUEST,
    KILLING,
    LOGOUT
};

struct SessionDelegate
{
    ~SessionDelegate() = default;

    virtual void onMessage(const Message& msg) const {};
    
    virtual void onLogon() const {};
    virtual void onLogout() const {};
};

class Session
{
public:
    Session(SessionSettings settings, Network& network, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store);
    ~Session();

    void start();
    void stop();

    bool isEnabled()
    {
        return m_enabled.load();
    }

    void setDelegate(std::shared_ptr<SessionDelegate> delegate)
    {
        m_delegate = delegate;
    }

    void setCache(std::unique_ptr<IFIXCache> cache)
    {
        m_cache = std::move(cache);
    }

    const SessionSettings& getSettings()
    {
        return m_settings;
    }

    void setSenderSeqNum(int num);
    void setTargetSeqNum(int num);

    int getSenderSeqNum();
    int getTargetSeqNum();

    void runUpdate();

    void send(Message& msg, SendCallback_T callback = SendCallback_T());

private:
    bool load();

    int populateMessage(Message& msg);

private:
    void onMessage(const std::string& msg);
    void processMessage(const Message& msg, long time);

    bool validateMessage(const Message& msg, long time);
    bool validateSeqNum(const Message& msg);

    void logout(const std::string& reason, bool terminate);
    void terminate(const std::string& reason);

    void handleLogon(const Message& msg);
    void handleResendRequest(const Message& msg);
    void handleSequenceReset(const Message& msg);

    void sendHeartbeat(long time, std::string testReqID = "");
    void sendLogon();
    void sendLogout(const std::string& reason, bool terminate);
    void sendResendRequest(int from, int to);
    void sendSequenceReset(int seqno, bool gapfill = true);
    void sendTestRequest();
    void sendReject(const Message& msg, SessionRejectReason reason, std::string text = "");

private:
    SessionSettings m_settings;

    std::shared_ptr<NetworkHandler> m_network;

    std::shared_ptr<SessionDelegate> m_delegate;

    std::shared_ptr<Dictionary> m_dictionary;

    LoggerHandle m_logger;

    SessionState m_state;

    std::unique_ptr<IFIXCache> m_cache;

    std::atomic<bool> m_enabled;
    std::mutex m_mutex;

    long m_lastSentHeartbeat = 0;
    long m_lastRecvHeartbeat = 0;
    long m_heartbeatInterval = 0;

    long m_lastLogon = 0;
    long m_logoutTime = 0;
    long m_logonInterval = 0;

    long m_lastReconnect = 0;
    long m_reconnectInterval = 0;

    long m_testReqID = 0;

    CREATE_LOGGER("Session");
};
