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

    virtual void cache(int seqnum, const Message& msg) = 0;

    using MessageConsumer = std::function<void(int, Message)>;
    virtual void getMessages(int begin, int end, MessageConsumer consumer) = 0;

    virtual void setSenderSeqNum(int num) = 0;
    virtual void setTargetSeqNum(int num) = 0;

    virtual int getSenderSeqNum() = 0;
    virtual int getTargetSeqNum() = 0;

    virtual int nextSenderSeqNum() = 0;
    virtual int nextTargetSeqNum() = 0;

    virtual std::map<int, Message>& getInboundQueue() = 0;

    virtual void load() = 0;
};

class MemoryCache : public IFIXCache
{
public:
    MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary, std::shared_ptr<IFIXStore> store);

    void cache(int seqnum, const Message& msg) override;

    void getMessages(int begin, int end, MessageConsumer consumer) override;

    void setSenderSeqNum(int num) override;
    void setTargetSeqNum(int num) override;

    int getSenderSeqNum() override;
    int getTargetSeqNum() override;

    int nextSenderSeqNum() override;
    int nextTargetSeqNum() override;

    std::map<int, Message>& getInboundQueue() override;

    void load() override;
    
private:
    const SessionSettings& m_settings;
    std::shared_ptr<Dictionary> m_dictionary;

    int m_senderSeqNum;
    int m_targetSeqNum;
    
    std::map<int, Message> m_messages;

    std::mutex m_mutex;

    std::map<int, Message> m_inboundQueue;

    StoreHandle m_store;

    CREATE_LOGGER("MemoryCache");
};