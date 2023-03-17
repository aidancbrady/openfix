#pragma once

#include "Config.h"
#include "Connection.h"
#include "Message.h"

#include <memory>

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

class Session
{
public:
    void setDelegate(std::shared_ptr<SessionDelegate> delegate)
    {
        m_delegate = delegate;
    }

private:
    std::shared_ptr<Connection> m_connection;

    std::weak_ptr<SessionDelegate> m_delegate;

    SessionSettings m_settings;
};
