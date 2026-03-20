#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>

#include "Dictionary.h"
#include "FIXStore.h"
#include "Message.h"

class IFIXCache
{
public:
    virtual ~IFIXCache() = default;

    virtual void cache(int seqnum, const std::string& wire) = 0;

    using MessageConsumer = std::function<void(int, const Message&)>;
    virtual void getMessages(int begin, int end, MessageConsumer consumer) const = 0;

    virtual void setSenderSeqNum(int num) = 0;
    virtual void setTargetSeqNum(int num) = 0;

    virtual int getSenderSeqNum() const = 0;
    virtual int getTargetSeqNum() const = 0;

    virtual int nextSenderSeqNum() = 0;
    virtual int nextTargetSeqNum() = 0;

    virtual std::map<int, Message>& getInboundQueue() = 0;

    virtual void flushSeqNums() = 0;

    virtual void load() = 0;
    virtual void reset() = 0;
};

class MemoryCache : public IFIXCache
{
public:
    MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary, std::shared_ptr<IFIXStore> store);

    void cache(int seqnum, const std::string& wire) override;

    void getMessages(int begin, int end, MessageConsumer consumer) const override;

    void setSenderSeqNum(int num) override;
    void setTargetSeqNum(int num) override;

    int getSenderSeqNum() const override;
    int getTargetSeqNum() const override;

    int nextSenderSeqNum() override;
    int nextTargetSeqNum() override;

    void flushSeqNums() override;

    std::map<int, Message>& getInboundQueue() override;

    void load() override;
    void reset() override;

private:
    const SessionSettings& m_settings;
    std::shared_ptr<Dictionary> m_dictionary;

    std::atomic<int> m_senderSeqNum;
    std::atomic<int> m_targetSeqNum;

    HashMapT<int, std::string> m_messages;

    bool m_seqNumsDirty = false;
    std::vector<int> m_pendingStoreSeqNums;

    std::map<int, Message> m_inboundQueue;

    StoreHandle m_store;

    CREATE_LOGGER("MemoryCache");
};