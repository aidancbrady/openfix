#pragma once

#include "Config.h"
#include "Message.h"
#include "Network.h"
#include "Dictionary.h"
#include "FIXCache.h"
#include "FIXStore.h"
#include "FIXLogger.h"

#include <openfix/Log.h>

#include <memory>
#include <atomic>
#include <functional>

enum class SessionType
{
    ACCEPTOR,
    INITIATOR,
};

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

class Session : MessageConsumer
{
public:
    Session(SessionSettings settings, std::shared_ptr<IFIXLogger>& logger, std::shared_ptr<IFIXStore>& store);
    ~Session();

    void start();
    void stop();

    bool isEnabled() override
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

    void send(Message& msg, SendCallback callback = SendCallback());

private:
    bool load();

    void populateMessage(Message& msg);

private:
    void processMessage(const std::string& msg) override;

    void sendHeartbeat(long time, std::string testReqID = "");

private:
    SessionSettings m_settings;

    std::shared_ptr<SessionDelegate> m_delegate;

    std::shared_ptr<ConnectionHandle> m_connection;
    std::shared_ptr<Dictionary> m_dictionary;

    LoggerHandle m_logger;
    StoreHandle m_store;

    SessionState m_state;

    std::unique_ptr<IFIXCache> m_cache;

    std::atomic<bool> m_enabled;

    long m_lastSentHeartbeat = 0;
    long m_lastRecvHeartbeat = 0;
    long m_heartbeatInterval = 0;

    long m_lastLogon = 0;
    long m_logoutTime = 0;
    long m_logonInterval = 0;

    long m_lastReconnect = 0;
    long m_reconnectInterval = 0;

    long m_testReqID = 0;

    SessionType m_sessionType;

    CREATE_LOGGER("Session");
};
