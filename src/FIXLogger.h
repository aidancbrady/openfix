#pragma once

#include "Message.h"
#include "Log.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

enum class Direction
{
    INBOUND,
    OUTBOUND
};

class IFIXLogger
{
public:
    virtual ~IFIXLogger() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void log(const Message& msg, Direction dir) = 0;
};

class FileLogger : public IFIXLogger
{
public:
    FileLogger();
    
    void start() override;
    void stop() override;

    void log(const Message& msg, Direction dir) override;
    
private:
    void process();

    std::thread m_thread;
    
    std::string m_buffer;
    std::string m_queue;

    std::condition_variable m_cv;
    std::mutex m_mutex;

    std::atomic<bool> m_enabled;

    CREATE_LOGGER("FileLogger");
};
