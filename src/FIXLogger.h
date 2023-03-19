#pragma once

#include "Message.h"
#include "Log.h"
#include "Config.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <memory>
#include <condition_variable>
#include <fstream>
#include <functional>

enum class Direction
{
    INBOUND,
    OUTBOUND
};

using LoggerFunction = std::function<void(const std::string& msg)>;

class LoggerHandle;

class IFIXLogger
{
public:
    virtual ~IFIXLogger() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    
    virtual LoggerHandle createLogger(const SessionSettings& settings) = 0;

protected:
    LoggerHandle createHandle(LoggerFunction evtLogger, LoggerFunction msgLogger) const;
};

struct LoggerInstance
{
    explicit LoggerInstance(std::string path) : m_path(std::move(path)) 
    {
        m_buffer.reserve(1024);
        m_queue.reserve(1024);
    }

    std::string m_buffer;
    std::string m_queue;

    std::ofstream m_stream;

    std::mutex m_mutex;

    std::string m_path;
};

class LoggerHandle
{
public:
    void logEvent(const std::string& event)
    {
        m_eventLogger(event + "\n");
    }

    void logMessage(const std::string& msg, Direction dir)
    {
        m_msgLogger(msg + "\n");
    }

private:
    LoggerHandle(LoggerFunction evtLogger, LoggerFunction msgLogger)
        : m_eventLogger(std::move(evtLogger)), m_msgLogger(std::move(msgLogger))
    {}

    LoggerFunction m_eventLogger;
    LoggerFunction m_msgLogger;

    friend class IFIXLogger;
};

class FileLogger : public IFIXLogger
{
public:
    FileLogger();
    ~FileLogger();

    void start() override;
    void stop() override;

    LoggerHandle createLogger(const SessionSettings& settings) override;
    
private:
    void process();

    std::thread m_thread;

    std::vector<std::unique_ptr<LoggerInstance>> m_instances;

    std::condition_variable m_cv;
    std::mutex m_mutex;

    std::atomic<bool> m_enabled;

    CREATE_LOGGER("FileLogger");
};
