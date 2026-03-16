#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "CpuOrchestrator.h"
#include "Log.h"
#include "Types.h"

using TimerID = uint32_t;
using Callback = std::function<void()>;

class Dispatcher;

class Worker
{
public:
    explicit Worker(bool spin = false)
        : m_stop(false)
        , m_spin(spin)
    {
        m_thread = std::thread([this] {
            CpuOrchestrator::bind(ThreadRole::WORKER);
            run();
        });
    }

    void run();
    void stop();
    void dispatch(Callback&& callback);

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    LockFreeQueueT<Callback> m_queue;
    std::thread m_thread;
    std::atomic<bool> m_stop;
    bool m_spin;

    friend class Dispatcher;

    CREATE_LOGGER("Worker");
};

class Dispatcher
{
public:
    Dispatcher()
        : Dispatcher(1, false)
    {}
    Dispatcher(size_t threadCount, bool spin = false);

    ~Dispatcher();

    void stop();

    void dispatch(Callback callback);
    void dispatch(Callback callback, int hash);

    size_t size() const { return m_workers.size(); }

private:
    std::vector<std::unique_ptr<Worker>> m_workers;

    CREATE_LOGGER("Dispatcher");
};

struct TimerEvent
{
    TimerEvent(Callback callback, uint64_t interval)
        : m_callback(std::move(callback))
        , m_repeating(true)
        , m_interval(interval)
    {}

    TimerEvent& repeating(bool repeating)
    {
        m_repeating = repeating;
        return *this;
    }

    TimerEvent& interval(uint64_t interval)
    {
        m_interval = interval;
        return *this;
    }

    Callback m_callback;
    bool m_repeating;
    uint64_t m_interval;
};

class Timer
{
public:
    Timer();
    ~Timer();

    TimerID schedule(TimerEvent event, uint64_t delay);

    bool erase(TimerID id);

private:
    void run();

    std::atomic<bool> m_stop;

    std::atomic<TimerID> m_timerCount;
    HashMapT<TimerID, TimerEvent> m_timers;
    std::map<uint64_t, std::vector<TimerID>> m_events;

    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::thread m_thread;

    CREATE_LOGGER("Timer");
};