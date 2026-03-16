#pragma once

#include <openfix/Dispatcher.h>
#include <openfix/Log.h>

#include <atomic>
#include <functional>
#include <memory>

#include "Config.h"
#include "Dictionary.h"
#include "FIXCache.h"
#include "FIXLogger.h"
#include "FIXStore.h"
#include "Fields.h"
#include "Message.h"
#include "Network.h"

enum class SessionState
{
    LOGON,
    READY,
    TEST_REQUEST,
    KILLING,
    LOGOUT
};

class Session;

struct SessionDelegate
{
    virtual ~SessionDelegate() = default;

    virtual void onMessage(Session& session, const Message& msg) {}
    virtual void onLogon(Session& session) {}
    virtual void onLogout(Session& session) {}
};

class Session
{
public:
    Session(SessionSettings settings, Network& network, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store,
            Dispatcher& dispatcher, int dispatchHash);
    ~Session();

    void start();
    void stop();

    void setDelegate(std::shared_ptr<SessionDelegate> delegate)
    {
        m_delegate = delegate;
    }

    void setCache(std::unique_ptr<IFIXCache> cache)
    {
        m_cache = std::move(cache);
    }

    const SessionSettings& getSettings() const
    {
        return m_settings;
    }

    void setSenderSeqNum(int num);
    void setTargetSeqNum(int num);

    int getSenderSeqNum() const;
    int getTargetSeqNum() const;

    bool isEnabled() const
    {
        return m_enabled.load(std::memory_order_acquire);
    }

    void setEnabled(bool enabled)
    {
        m_enabled.store(enabled, std::memory_order_release);
    }

    const std::shared_ptr<NetworkHandler>& getNetwork()
    {
        return m_network;
    }

    SessionState getState() const
    {
        return m_state;
    }

    void runUpdate();

    Message createMessage(const std::string& msgType) const
    {
        return m_dictionary->create(msgType);
    }

    void send(Message& msg, SendCallback_T callback = SendCallback_T());

private:
    bool load();
    void reset();

    int populateMessage(Message& msg);
    void runMessageRecovery(int from, int to);

    void internal_update();
    
    void internal_send(const Message& msg, SendCallback_T callback);
    void internal_send(std::string msg, SendCallback_T callback);

private:
    void onMessage(std::string msg);
    void processMessage(const Message& msg, long time);

    bool validateMessage(const Message& msg, long time);
    bool validateSeqNum(const Message& msg);

    void logout(const std::string& reason, bool terminate);
    void terminate(const std::string& reason);

    void handleLogon(const Message& msg);
    void handleResendRequest(const Message& msg);
    void handleSequenceReset(const Message& msg);

    void sendHeartbeat(long time, std::string testReqID = "");
    void sendLogon(bool reset);
    void sendLogout(const std::string& reason, bool terminate);
    void sendResendRequest(int from, int to);
    void sendSequenceReset(int seqno, int new_seqno, bool gapfill = true);
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

    Dispatcher& m_dispatcher;
    int m_dispatchHash;

    long m_lastSentHeartbeat = 0;
    long m_lastRecvHeartbeat = 0;
    long m_lastSentTestRequest = 0;

    long m_heartbeatInterval = 0;

    long m_lastLogon = 0;
    long m_logoutTime = 0;
    long m_logonInterval = 0;

    long m_lastReconnect = 0;
    long m_reconnectInterval = 0;

    long m_testReqID = 0;

    CREATE_LOGGER("Session");
};
