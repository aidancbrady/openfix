#pragma once

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <sstream>

struct Logger
{
    static void initialize()
    {
        static bool initialized = false;
        if (!initialized) {
            spdlog::set_level(spdlog::level::trace);
            spdlog::set_pattern("[%H:%M:%S] [%n] [%l] [%t] %v");
            initialized = true;
        }
    }

    static inline std::shared_ptr<spdlog::logger> get_logger(const std::string& name)
    {
        auto logger = spdlog::get(name);
        if (!logger)
            return spdlog::stdout_color_mt(name);
        return logger;
    }
};

#define __LOWER(TOK) __LOWER_ ## TOK

#define __LOWER_TRACE trace
#define __LOWER_DEBUG debug
#define __LOWER_INFO info
#define __LOWER_WARN warn
#define __LOWER_ERROR error
#define __LOWER_FATAL critical

// spdlog level constants via token pasting (avoids macro parameter collision with spdlog::level::)
#define __SPDLOG_LEVEL(TOK) __SPDLOG_LEVEL_ ## TOK
#define __SPDLOG_LEVEL_TRACE spdlog::level::trace
#define __SPDLOG_LEVEL_DEBUG spdlog::level::debug
#define __SPDLOG_LEVEL_INFO  spdlog::level::info
#define __SPDLOG_LEVEL_WARN  spdlog::level::warn
#define __SPDLOG_LEVEL_ERROR spdlog::level::err
#define __SPDLOG_LEVEL_FATAL spdlog::level::critical

#define CREATE_LOGGER(name) static inline std::shared_ptr<spdlog::logger> __LOGGER__ = Logger::get_logger(#name);

#define LOG_TRACE(arg1, ...) __LOG(arg1, ##__VA_ARGS__, TRACE, _EXPLICIT, _IMPLICIT)
#define LOG_DEBUG(arg1, ...) __LOG(arg1, ##__VA_ARGS__, DEBUG, _EXPLICIT, _IMPLICIT)
#define LOG_INFO(arg1, ...) __LOG(arg1, ##__VA_ARGS__, INFO, _EXPLICIT, _IMPLICIT)
#define LOG_WARN(arg1, ...) __LOG(arg1, ##__VA_ARGS__, WARN, _EXPLICIT, _IMPLICIT)
#define LOG_ERROR(arg1, ...) __LOG(arg1, ##__VA_ARGS__, ERROR, _EXPLICIT, _IMPLICIT)
#define LOG_FATAL(arg1, ...) __LOG(arg1, ##__VA_ARGS__, FATAL, _EXPLICIT, _IMPLICIT)

#define __LOG(arg1, arg2, level, suffix, ...) __LOG##suffix(arg1, arg2, level)
#define __LOG_IMPLICIT(msg, level, ...) __LOG_IMPL(__LOGGER__, msg, level)
#define __LOG_EXPLICIT(logger, msg, level) __LOG_IMPL(Logger::get_logger(#logger), msg, level)

#define __LOG_IMPL(logger, msg, level)                                                \
    do {                                                                              \
        if (logger->should_log(__SPDLOG_LEVEL(level))) {                              \
            std::ostringstream ostr;                                                  \
            ostr << msg;                                                              \
            logger->__LOWER(level)(ostr.str());                                       \
        }                                                                             \
    } while (0);
