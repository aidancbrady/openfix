#pragma once

#include "FIXLogger.h"
#include "FIXStore.h"

#include <memory>

struct ApplicationDelegate
{

};

class Application
{
public:
    Application(std::shared_ptr<IFIXLogger> logger, std::shared_ptr<IFIXStore> store);
    virtual ~Application() = default;

    void setDelegate(std::shared_ptr<ApplicationDelegate> delegate)
    {
        m_delegate = delegate;
    }

private:
    std::shared_ptr<IFIXLogger> m_logger;
    std::shared_ptr<IFIXStore> m_store;

    std::weak_ptr<ApplicationDelegate> m_delegate;
};
