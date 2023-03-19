#pragma once

#include "Config.h"
#include "Message.h"
#include "Network.h"
#include "Dictionary.h"

#include <memory>
#include <atomic>

enum class SessionType
{
    ACCEPTOR,
    INITIATOR,
};

enum class SessionState
{
    LOGON,
    READY,
    LOGOUT
};

struct SessionDelegate
{
    ~SessionDelegate() = default;

    virtual void onMessage(const Message& msg) const {};
};

class Session : MessageConsumer
{
public:
    Session(SessionSettings settings);
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

    void runUpdate();

private:
    void processMessage(const std::string& msg) override;

    std::shared_ptr<Dictionary> m_dictionary;

    std::shared_ptr<SessionDelegate> m_delegate;

    std::weak_ptr<ConnectionHandle> m_connection;

    std::atomic<bool> m_enabled;

    int m_senderSeqNum;
    int m_targetSeqNum;

    SessionType m_sessionType;

    SessionSettings m_settings;
};
