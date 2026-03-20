#pragma once

#include <atomic>
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
    explicit WriterInstance(std::string path, bool formatFIXMessages = false)
        : m_path(std::move(path))
        , m_shouldReset(false)
        , m_dirty(false)
        , m_formatFIXMessages(formatFIXMessages)
    {
        m_buffer.reserve(BUF_SIZE);
        m_queue.reserve(BUF_SIZE);
    }

    void write(const std::string& text);
    void writeRaw(const char* prefix, size_t prefixLen, const std::string& body);
    void writeMessage(int64_t epoch_us, bool inbound, const std::string& msg);
    void writeMessage(int64_t epoch_us, bool inbound, std::string&& msg);

    void reset()
    {
        m_shouldReset.store(true, std::memory_order_release);
        m_dirty.store(true, std::memory_order_release);
    }

private:
    std::string m_buffer;
    std::string m_queue;

    std::ofstream m_stream;

    std::mutex m_mutex;

    std::string m_path;

    std::vector<LogEntry> m_logEntryQueue;
    std::vector<LogEntry> m_logEntryBuffer;

    std::atomic<bool> m_shouldReset;
    std::atomic<bool> m_dirty;
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
    void flushInstances();

    std::thread m_thread;

    HashMapT<std::string, std::unique_ptr<WriterInstance>> m_instances;

    std::atomic<bool> m_enabled;

    CREATE_LOGGER("FileWriter");
};

#undef BUF_SIZE
