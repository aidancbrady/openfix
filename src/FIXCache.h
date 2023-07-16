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

    virtual void setSenderSeqNum(int num) = 0;
    virtual void setTargetSeqNum(int num) = 0;

    virtual int getSenderSeqNum() = 0;
    virtual int getTargetSeqNum() = 0;

    virtual std::map<int, Message>& getInboundQueue();

    virtual void load(const SessionData& data) = 0;
};

class MemoryCache : public IFIXCache
{
public:
    MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary);

    void cache(const Message& msg) override;

    void setSenderSeqNum(int num) override;
    void setTargetSeqNum(int num) override;

    int getSenderSeqNum() override;
    int getTargetSeqNum() override;

    std::map<int, Message>& getInboundQueue() override;

    void load(const SessionData& data) override;
    
private:
    const SessionSettings& m_settings;
    std::shared_ptr<Dictionary> m_dictionary;

    int m_senderSeqNum;
    int m_targetSeqNum;
    
    std::map<int, Message> m_messages;

    std::mutex m_seqNumMutex;

    std::map<int, Message> m_inboundQueue;

    CREATE_LOGGER("MemoryCache");
};