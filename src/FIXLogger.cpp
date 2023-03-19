#include "FIXLogger.h"

// 1KB log buffer
#define BUF_SIZE = 1024

LoggerHandle IFIXLogger::createHandle(LoggerFunction evtLogger, LoggerFunction msgLogger) const
{
    return {std::move(evtLogger), std::move(msgLogger)};
}

LoggerHandle FileLogger::createLogger(const SessionSettings& settings)
{
    std::string sessionID = settings.getString(SessionSettings::SENDER_COMP_ID) + "-" + settings.getString(SessionSettings::TARGET_COMP_ID);
    
    m_instances.push_back(std::make_unique<LoggerInstance>(sessionID + ".event.log"));
    auto& evtLogger = *m_instances[m_instances.size()-1];

    m_instances.push_back(std::make_unique<LoggerInstance>(sessionID + ".messages.log"));
    auto& msgLogger = *m_instances[m_instances.size()-1];

    auto evtFunction = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(evtLogger.m_mutex);
        evtLogger.m_queue.append(msg);
        m_cv.notify_one();
    };

    auto msgFunction = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(msgLogger.m_mutex);
        msgLogger.m_queue.append(msg);
        m_cv.notify_one();
    };

    return createHandle(std::move(evtFunction), std::move(msgFunction));
}

FileLogger::FileLogger() : m_enabled(true) {}

FileLogger::~FileLogger()
{
    stop();
}

void FileLogger::start()
{
    m_thread = std::thread([&]() {
        process();
    });
}

void FileLogger::stop()
{
    m_enabled.store(false, std::memory_order_release);
    m_cv.notify_one();
    m_thread.join();

    // close open files
    for (auto& instance : m_instances)
        instance->m_stream.close();
}

void FileLogger::process()
{
    while (m_enabled.load(std::memory_order_acquire))
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock);

        // we might have received a shutdown call
        if (!m_enabled.load(std::memory_order_acquire))
            return;

        // find instances ready for writing
        for (auto& instancePtr : m_instances)
        {
            auto& instance = *instancePtr;
            // swap buffer
            {
                std::lock_guard<std::mutex> lock(instance.m_mutex);
                if (!instance.m_queue.empty())
                    instance.m_queue.swap(instance.m_buffer);
            }

            if (instance.m_buffer.empty())
                continue;
   
            // write
            if (!instance.m_stream.is_open())
                instance.m_stream.open(instance.m_path, std::ofstream::out | std::ofstream::app);
            instance.m_stream << instance.m_buffer;

            // clear the buffer
            instance.m_buffer.clear();
        }
    }
}
