#pragma once

#include "FIXLogger.h"
#include "FIXStore.h"

#include <memory>
#include <thread>
#include <atomic>

struct ApplicationDelegate
{

};

class Application
{
public:
    Application(std::shared_ptr<IFIXLogger> logger, std::shared_ptr<IFIXStore> store);
    virtual ~Application();

    void setDelegate(std::shared_ptr<ApplicationDelegate> delegate)
    {
        m_delegate = delegate;
    }

    void start();
    void stop();

private:
    void runUpdate();

    std::shared_ptr<IFIXLogger> m_logger;
    std::shared_ptr<IFIXStore> m_store;

    std::atomic<bool> m_running;

    std::thread m_updateThread;

    std::weak_ptr<ApplicationDelegate> m_delegate;
};
