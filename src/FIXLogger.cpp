#include "FIXLogger.h"

// 1KB log buffer
#define BUF_SIZE = 1024

FileLogger::FileLogger() : m_enabled(true)
{
    m_buffer.reserve(1024);
    m_queue.reserve(1024);
}

void FileLogger::start()
{
    m_thread = std::thread([&]() {
        process();
    });
}

void FileLogger::stop()
{
    m_cv.notify_one();
    m_thread.join();
}

void FileLogger::log(const Message& msg, Direction dir)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.append(msg.toString());
    m_cv.notify_one();
}

void FileLogger::process()
{
    while (m_enabled.load(std::memory_order_acquire))
    {
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock);

            if (m_queue.empty())
                continue;

            m_queue.swap(m_buffer);
        }

        // we might have received a shutdown call
        if (!m_enabled.load(std::memory_order_acquire))
            return;

        // write
    }
}
