#include "Dispatcher.h"

#include <cstdlib>

#include "CpuOrchestrator.h"
#include "Utils.h"

////////////////////////////////////////////
//               Dispatcher               //
////////////////////////////////////////////

Dispatcher::Dispatcher(size_t threads, bool spin)
{
    for (size_t i = 0; i < threads; ++i)
        m_workers.push_back(std::make_unique<Worker>(spin));
}

Dispatcher::~Dispatcher()
{
    stop();
}

void Dispatcher::stop()
{
    for (auto& worker : m_workers)
        worker->stop();
    for (auto& worker : m_workers)
        if (worker->m_thread.joinable())
            worker->m_thread.join();
}

void Worker::run()
{
    while (!m_stop.load(std::memory_order_acquire)) {
        Callback task;
        if (m_queue.try_dequeue(task)) {
            task();
            continue;
        }

        if (m_spin) {
            // Busy-poll: avoid kernel CV wakeup overhead at the cost of a dedicated core.
            // _mm_pause / __builtin_ia32_pause reduces power draw and improves
            // performance on hyperthreaded cores by hinting the CPU we're spinning.
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#else
            std::this_thread::yield();
#endif
            continue;
        }

        // Normal mode: sleep on CV until work arrives
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]() { return m_stop.load() || m_queue.size_approx() > 0; });
        }

        if (m_stop.load())
            return;
        if (m_queue.try_dequeue(task))
            task();
    }
}

void Worker::dispatch(Callback&& callback)
{
    m_queue.enqueue(std::move(callback));

    if (!m_spin) {
        std::lock_guard lock(m_mutex);
        m_cv.notify_one();
    }
}

void Worker::stop()
{
    m_stop.store(true, std::memory_order_release);

    if (!m_spin) {
        std::lock_guard lock(m_mutex);
        m_cv.notify_one();
    }
}

void Dispatcher::dispatch(Callback callback)
{
    dispatch(std::move(callback), std::rand());
}

void Dispatcher::dispatch(Callback callback, int hash)
{
    m_workers[hash % m_workers.size()]->dispatch(std::move(callback));
}

////////////////////////////////////////////
//                 Timer                  //
////////////////////////////////////////////

Timer::Timer()
    : m_stop(false)
    , m_timerCount(0)
{
    m_thread = std::thread([&]() {
        CpuOrchestrator::bind(ThreadRole::TIMER);
        run();
    });
}

Timer::~Timer()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop.store(true, std::memory_order_release);
        m_cv.notify_one();
    }

    m_thread.join();
}

void Timer::run()
{
    uint64_t time, next;
    while (!m_stop.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        time = Utils::getEpochMillis();

        while (!m_events.empty()) {
            auto it = m_events.begin();
            if (time >= it->first) {
                for (const auto id : it->second) {
                    auto timer_it = m_timers.find(id);
                    if (timer_it != m_timers.end()) {
                        const auto& timer = timer_it->second;
                        timer.m_callback();

                        if (!timer.m_repeating)
                            m_timers.erase(id);
                        else
                            m_events[time + timer.m_interval].push_back(id);
                    }
                }

                m_events.erase(it);
                continue;
            }
            next = it->first;
            break;
        }

        if (next > time)
            m_cv.wait_for(lock, std::chrono::milliseconds(next - time));
        else
            m_cv.wait(lock);
    }
}

TimerID Timer::schedule(TimerEvent event, uint64_t delay)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto id = ++m_timerCount;
    m_timers.emplace(id, std::move(event));
    m_events[Utils::getEpochMillis() + delay].push_back(id);
    m_cv.notify_one();
    return id;
}

bool Timer::erase(TimerID id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool ret = m_timers.erase(id) > 0;
    m_cv.notify_one();
    return ret;
}