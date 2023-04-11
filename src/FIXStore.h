#pragma once

#include "Config.h"

#include <openfix/Log.h>
#include <openfix/FileUtils.h>

#include <map>
#include <functional>

struct SessionData
{
    std::map<int, std::string> m_messages;

    int m_senderSeqNum;
    int m_targetSeqNum;
};

using StoreFunction = std::function<void(const std::string&)>;

class IFIXStore;

class StoreHandle
{
public:
    void store(const std::string& msg)
    {
        m_storeFunc(msg);
    }

    SessionData load();

private:
    StoreHandle(const SessionSettings& settings, StoreFunction storeFunc)
        : m_settings(settings)
        , m_storeFunc(std::move(storeFunc))
    {}

    const SessionSettings& m_settings;
    StoreFunction m_storeFunc;

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
    StoreHandle createHandle(const SessionSettings& settings, StoreFunction storeFunc) const
    {
        return {settings, std::move(storeFunc)};
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
