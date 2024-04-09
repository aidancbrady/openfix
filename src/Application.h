#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "AdminWebsite.h"
#include "FIXLogger.h"
#include "FIXStore.h"
#include "Network.h"
#include "Session.h"
#include "lib/Dispatcher.h"

struct ApplicationDelegate
{
};

class Application
{
public:
    Application();
    Application(std::shared_ptr<IFIXLogger> logger, std::shared_ptr<IFIXStore> store);

    virtual ~Application();

    void setDelegate(std::shared_ptr<ApplicationDelegate> delegate)
    {
        m_delegate = delegate;
    }

    void createSession(const std::string& sessionName, const SessionSettings& settings);

    std::shared_ptr<Session> getSession(const std::string& sessionName)
    {
        auto it = m_sessionMap.find(sessionName);
        if (it == m_sessionMap.end())
            return nullptr;
        return it->second;
    }

    void start();
    void stop();

private:
    void runUpdate();

    std::shared_ptr<IFIXLogger> m_logger;
    std::shared_ptr<IFIXStore> m_store;

    std::unique_ptr<Network> m_network;

    std::atomic<bool> m_running;

    std::thread m_updateThread;

    std::unordered_map<std::string, std::shared_ptr<Session>> m_sessionMap;

    std::unique_ptr<AdminWebsite> m_adminWebsite;

    std::weak_ptr<ApplicationDelegate> m_delegate;

    friend class AdminWebsite;

    CREATE_LOGGER("Application");
};
