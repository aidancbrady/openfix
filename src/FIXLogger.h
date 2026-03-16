#pragma once

#include <openfix/FileUtils.h>
#include <openfix/Log.h>
#include <openfix/Utils.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Config.h"
#include "Message.h"

enum class Direction
{
    INBOUND,
    OUTBOUND
};

using LoggerFunction = std::function<void(const std::string& msg)>;
using MsgLoggerFunction = std::function<void(int64_t epoch_us, bool inbound, const std::string& msg)>;

class LoggerHandle;

class IFIXLogger
{
public:
    virtual ~IFIXLogger() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual LoggerHandle createLogger(const SessionSettings& settings) = 0;

protected:
    LoggerHandle createHandle(LoggerFunction evtLogger, MsgLoggerFunction msgLogger) const;
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
        // Capture raw epoch microseconds (~20ns vDSO call).
        // Timestamp formatting is deferred to the FileWriter thread.
        const auto epoch_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        m_msgLogger(epoch_us, dir == Direction::INBOUND, msg);
    }

private:
    LoggerHandle(LoggerFunction evtLogger, MsgLoggerFunction msgLogger)
        : m_eventLogger(std::move(evtLogger))
        , m_msgLogger(std::move(msgLogger))
    {}

    LoggerFunction m_eventLogger;
    MsgLoggerFunction m_msgLogger;

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
