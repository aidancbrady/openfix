#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include "Log.h"

static void signalHandler(int signum);

class SignalHandler
{
public:
    static void static_wait()
    {
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        instance().wait();
    }

    void on_signal(int signum)
    {
        int cnt = m_signal_count.fetch_add(1) + 1;

        LOG_INFO("SIGNAL", "Signal (" << signum << ") received -- count=" << cnt);

        if (cnt == 2) {
            LOG_WARN("SIGNAL", "Received exit signal " << cnt << " times, attempting clean exit...");
            std::quick_exit(1);
        } else if (cnt >= 3) {
            LOG_WARN("SIGNAL", "Received exit signal " << cnt << " times, exiting...");
            exit(1);
        }
    }

    void wait()
    {
        while (m_signal_count.load() == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    static SignalHandler& instance()
    {
        static SignalHandler handler;
        return handler;
    }

private:
    std::atomic<int> m_signal_count = 0;
};

void signalHandler(int signum)
{
    SignalHandler::instance().on_signal(signum);
}
