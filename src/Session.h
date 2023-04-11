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

    void runUpdate();

    void send(const Message& msg);

private:
    void processMessage(const std::string& msg) override;

    bool load();

private:
    SessionSettings m_settings;

    std::shared_ptr<SessionDelegate> m_delegate;

    std::shared_ptr<ConnectionHandle> m_connection;
    std::shared_ptr<Dictionary> m_dictionary;

    LoggerHandle m_logger;
    StoreHandle m_store;

    std::unique_ptr<IFIXCache> m_cache;

    std::atomic<bool> m_enabled;

    long m_lastHeartbeat;

    SessionType m_sessionType;

    CREATE_LOGGER("Session");
};
