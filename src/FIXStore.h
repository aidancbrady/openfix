#pragma once

#include <openfix/FileUtils.h>
#include <openfix/Log.h>

#include <functional>
#include <map>

#include "Config.h"

struct SessionData
{
    std::map<int, std::string> m_messages;

    int m_senderSeqNum = 1;
    int m_targetSeqNum = 1;
};

using WriteFunction = std::function<void(const std::string&)>;

class IFIXStore;

class StoreHandle
{
public:
    void store(int seqnum, const std::string& msg);
    void setSenderSeqNum(int num);
    void setTargetSeqNum(int num);

    SessionData load();
    void reset();

private:
    StoreHandle(const SessionSettings& settings, WriterInstance& writer, std::string path)
        : m_settings(settings)
        , m_writer(writer)
        , m_path(std::move(path))
    {}

    void write(const std::string& msg)
    {
        m_writer.write(msg);
    }

    const SessionSettings& m_settings;
    WriterInstance& m_writer;
    std::string m_path;

    CREATE_LOGGER("StoreHandle");

    friend class IFIXStore;
};

class IFIXStore
{
public:
    virtual ~IFIXStore() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual StoreHandle createStore(const SessionSettings& settings) = 0;

protected:
    StoreHandle createHandle(const SessionSettings& settings, WriterInstance& writer, std::string path) const
    {
        return {settings, writer, std::move(path)};
    }
};

class FileStore : public IFIXStore
{
public:
    FileStore() = default;
    ~FileStore();

    void start() override;
    void stop() override;

    StoreHandle createStore(const SessionSettings& settings) override;

private:
    FileWriter m_writer;

    CREATE_LOGGER("FileStore");
};
