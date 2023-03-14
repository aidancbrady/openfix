#pragma once

#include "Session.h"

#include <unordered_map>
#include <memory>

class SessionRegistry
{
public:
    const std::shared_ptr<Session>& createSession(std::string name, const SessionSettings& settings);

private:
    std::unordered_map<std::string, std::shared_ptr<Session>> m_sessionMap;
};
