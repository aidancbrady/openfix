#pragma once

#include <openfix/FileUtils.h>
#include <openfix/Log.h>

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Config.h"
#include "Message.h"

enum class Direction { INBOUND, OUTBOUND };

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
        : m_eventLogger(std::move(evtLogger))
        , m_msgLogger(std::move(msgLogger))
    {}

    LoggerFunction m_eventLogger;
    LoggerFunction m_msgLogger;

    friend class IFIXLogger;
};

class FileLogger : public IFIXLogger
{
public:
    FileLogger() = default;
    ~FileLogger();

    void start() override;
    void stop() override;

    LoggerHandle createLogger(const SessionSettings& settings) override;

private:
    FileWriter m_writer;

    CREATE_LOGGER("FileLogger");
};
