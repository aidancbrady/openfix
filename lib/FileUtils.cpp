#include "FileUtils.h"

#include <filesystem>

FileWriter::FileWriter()
    : m_enabled(false)
{}

FileWriter::~FileWriter()
{
    stop();
}

void FileWriter::start()
{
    m_thread = std::thread([&]() {
        m_enabled.store(true, std::memory_order_release);
        process();
    });
}

void FileWriter::stop()
{
    if (m_enabled.load(std::memory_order_acquire)) {
        m_enabled.store(false, std::memory_order_release);
        m_cv.notify_one();
        m_thread.join();

        // close open files
        for (auto& [_, instance] : m_instances)
            instance->m_stream.close();
    }
}

std::unique_ptr<WriterInstance>& FileWriter::createInstance(const std::string& fileName)
{
    auto it = m_instances.find(fileName);
    if (it != m_instances.end())
        return it->second;

    it = m_instances.emplace(fileName, std::make_unique<WriterInstance>(fileName, m_cv)).first;
    return it->second;
}

void FileWriter::process()
{
    while (m_enabled.load(std::memory_order_acquire)) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock);

        // we might have received a shutdown call
        if (!m_enabled.load(std::memory_order_acquire))
            return;

        // find instances ready for writing
        for (auto& [_, instancePtr] : m_instances) {
            auto& instance = *instancePtr;
            // swap buffer
            {
                std::lock_guard<std::mutex> lock(instance.m_mutex);

                if (instance.m_shouldReset) {
                    instance.m_shouldReset = false;

                    instance.m_queue.clear();
                    instance.m_buffer.clear();

                    // re-open the file with truncation option to wipe
                    instance.m_stream.close();
                    instance.m_stream.open(instance.m_path, std::ofstream::out | std::ofstream::trunc);
                    instance.m_stream.close();
                    continue;
                } else if (!instance.m_queue.empty()) {
                    instance.m_queue.swap(instance.m_buffer);
                }
            }

            if (instance.m_buffer.empty())
                continue;

            // write
            if (!instance.m_stream.is_open() || !instance.m_stream.good()) {
                if (instance.m_stream.is_open())
                    instance.m_stream.close();  // close the stream if it's in an error state

                auto dir_it = instance.m_path.rfind(std::filesystem::path::preferred_separator);
                std::filesystem::create_directories(instance.m_path.substr(0, dir_it));

                LOG_DEBUG("Opening for writing: " << instance.m_path);
                instance.m_stream.open(instance.m_path, std::ofstream::out | std::ofstream::app);
                if (!instance.m_stream.good()) {
                    LOG_ERROR("Failed to open file for writing: " << instance.m_path);
                    continue;
                }
            }

            instance.m_stream << instance.m_buffer;
            instance.m_stream.flush();

            // clear the buffer
            instance.m_buffer.clear();
        }
    }
}

void WriterInstance::write(const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.append(text);
    m_cv.notify_one();
}
