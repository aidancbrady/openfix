#pragma once

#include <iostream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

struct Logger
{
    static void initialize()
    {
        static bool initialized = false;
        if (!initialized)
        {
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
        std::ostringstream ostr;                                                      \
        ostr << msg;                                                                  \
        logger->__LOWER(level)(ostr.str());                                           \
    } while (0);
