#include "Application.h"

#include <unistd.h>

Application::Application()
    : Application(std::make_shared<FileLogger>(), std::make_shared<FileStore>())
{
    const int websitePort = PlatformSettings::getLong(PlatformSettings::ADMIN_WEBSITE_PORT);
    if (websitePort > 0) {
        m_adminWebsite = std::make_unique<AdminWebsite>(*this, websitePort);
    }
}

Application::Application(std::shared_ptr<IFIXLogger> logger, std::shared_ptr<IFIXStore> store)
    : m_logger(std::move(logger))
    , m_store(std::move(store))
    , m_network(std::make_unique<Network>())
{}

Application::~Application()
{
    stop();
}

void Application::start()
{
    if (m_running.load(std::memory_order_acquire))
        return;
    m_running.store(true, std::memory_order_release);

    m_logger->start();
    m_store->start();

    m_network->start();

    // Background thread handles disconnected session updates (reconnect logic).
    // Connected sessions are updated inline by the ReaderThread via NetworkDelegate.
    m_updateThread = std::thread([&]() {
        while (m_running.load(std::memory_order_acquire)) {
            runUpdate();
            ::usleep(PlatformSettings::getLong(PlatformSettings::UPDATE_DELAY) * 1000);
        }
    });
}

void Application::stop()
{
    if (!m_running.load(std::memory_order_acquire))
        return;
    m_running.store(false, std::memory_order_release);

    // stop all sessions while network is still alive
    for (auto& [_, session] : m_sessionMap)
        session->stop();

    m_network->stop();

    m_logger->stop();
    m_store->stop();

    m_updateThread.join();
}

std::shared_ptr<Session> Application::createSession(const std::string& sessionName, const SessionSettings& settings)
{
    if (m_sessionMap.find(sessionName) != m_sessionMap.end())
        throw std::runtime_error("Session already exists with name: " + sessionName);

    auto session = std::make_shared<Session>(settings, *m_network, m_logger, m_store);
    m_sessionMap[sessionName] = session;
    return session;
}

void Application::runUpdate()
{
    for (auto& [_, session] : m_sessionMap) {
        if (!session->getNetwork()->isConnected())
            session->onNetworkUpdate();
    }
}
