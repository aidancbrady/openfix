#pragma once

#include "Message.h"
#include "Dictionary.h"
#include "FIXStore.h"

#include <map>
#include <memory>

class IFIXCache
{
public:
    virtual ~IFIXCache() = default;

    virtual void cache(const Message& msg) = 0;

    virtual void load(const SessionData& data) = 0;
};

class MemoryCache : public IFIXCache
{
public:
    MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary);

    void cache(const Message& msg) override;

    void load(const SessionData& data) override;
    
private:
    const SessionSettings& m_settings;
    std::shared_ptr<Dictionary> m_dictionary;

    int m_senderSeqNum;
    int m_targetSeqNum;
    
    std::map<int, Message> m_messages;

    CREATE_LOGGER("MemoryCache");
};