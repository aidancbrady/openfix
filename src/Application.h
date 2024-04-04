#pragma once

#include "FIXLogger.h"
#include "FIXStore.h"
#include "Session.h"
#include "Network.h"
#include "lib/Dispatcher.h"

#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>

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

    std::weak_ptr<ApplicationDelegate> m_delegate;

    CREATE_LOGGER("Application");
};
