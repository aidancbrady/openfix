#include "Application.h"

#include <unistd.h>

Application::Application(std::shared_ptr<IFIXLogger> logger, std::shared_ptr<IFIXStore> store)
    : m_logger(std::move(logger)), m_store(std::move(store))
{
   
}

Application::~Application() {}

void Application::start()
{
    m_logger->start();
    m_store->start();

    m_updateThread = std::thread([&]() {
        runUpdate();
        ::usleep(PlatformSettings::getLong(PlatformSettings::UPDATE_DELAY));
    });
}

void Application::stop()
{
    m_logger->stop();
    m_store->stop();
}

void Application::runUpdate()
{
    for (auto& [_, session] : m_sessionMap)
        session->runUpdate();
}
