#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Log.h"
#include "Types.h"

struct LogEntry
{
    int64_t epoch_us;
    bool inbound;
    std::string msg;
};

// 1KB write buffer
#define BUF_SIZE 1024

class FileWriter;

class WriterInstance
{
public:
    explicit WriterInstance(std::string path, std::condition_variable& cv, bool formatFIXMessages = false)
        : m_path(std::move(path))
        , m_cv(cv)
        , m_shouldReset(false)
        , m_formatFIXMessages(formatFIXMessages)
    {
        m_buffer.reserve(BUF_SIZE);
        m_queue.reserve(BUF_SIZE);
    }

    void write(const std::string& text);
    void writeMessage(int64_t epoch_us, bool inbound, const std::string& msg);

    void reset()
    {
        m_shouldReset = true;
        m_cv.notify_one();
    }

private:
    std::string m_buffer;
    std::string m_queue;

    std::ofstream m_stream;

    std::mutex m_mutex;

    std::string m_path;

    std::condition_variable& m_cv;

    std::vector<LogEntry> m_logEntryQueue;
    std::vector<LogEntry> m_logEntryBuffer;

    std::atomic<bool> m_shouldReset;
    bool m_formatFIXMessages;

    friend class FileWriter;
};

class FileWriter
{
public:
    FileWriter();
    ~FileWriter();

    void start();
    void stop();

    std::unique_ptr<WriterInstance>& createInstance(const std::string& fileName, bool formatFIXMessages = false);

private:
    void process();

    std::thread m_thread;

    HashMapT<std::string, std::unique_ptr<WriterInstance>> m_instances;

    std::condition_variable m_cv;
    std::mutex m_mutex;

    std::atomic<bool> m_enabled;

    CREATE_LOGGER("FileWriter");
};

#undef BUF_SIZE
