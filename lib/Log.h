#pragma once

#include <iostream>

enum class LogLevel
{
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class LogSettings
{
public:
    static inline LogLevel LOG_LEVEL = LogLevel::INFO;
};

class Logger
{
public:
    const std::string& name()
    {
        return m_name;
    }

    const std::string& context()
    {
        return m_context;
    }
private:
    std::string m_name;
    std::string m_context;
};

#define CREATE_LOGGER(name) static inline std::string __LOGGER_NAME__ = name;

#define LOG_TRACE(arg1, ...) __LOG(arg1, ##__VA_ARGS__, TRACE, _EXPLICIT, _IMPLICIT)
#define LOG_DEBUG(arg1, ...) __LOG(arg1, ##__VA_ARGS__, DEBUG, _EXPLICIT, _IMPLICIT)
#define LOG_INFO(arg1, ...) __LOG(arg1, ##__VA_ARGS__, INFO, _EXPLICIT, _IMPLICIT)
#define LOG_WARN(arg1, ...) __LOG(arg1, ##__VA_ARGS__, WARN, _EXPLICIT, _IMPLICIT)
#define LOG_ERROR(arg1, ...) __LOG(arg1, ##__VA_ARGS__, ERROR, _EXPLICIT, _IMPLICIT)
#define LOG_FATAL(arg1, ...) __LOG(arg1, ##__VA_ARGS__, FATAL, _EXPLICIT, _IMPLICIT)

#define __LOG(arg1, arg2, level, suffix, ...) __LOG##suffix(arg1, arg2, level)
#define __LOG_IMPLICIT(msg, level, ...) __LOG_IMPL(__LOGGER_NAME__, msg, level)
#define __LOG_EXPLICIT(logger, msg, level) __LOG_IMPL(logger, msg, level)

#define __LOG_IMPL(logger, msg, level)           \
    if (LogLevel::level >= LogSettings::LOG_LEVEL)    \
        std::cout << "[" << logger << "] " << msg << std::endl;
