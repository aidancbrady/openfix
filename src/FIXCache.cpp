#include "FIXCache.h"

MemoryCache::MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary, std::shared_ptr<IFIXStore> store)
    : m_settings(settings)
    , m_dictionary(std::move(dictionary))
    , m_senderSeqNum(1)
    , m_targetSeqNum(1)
    , m_store(store->createStore(settings))
{}

void MemoryCache::load()
{
    auto data = m_store.load();

    m_messages.clear();

    m_senderSeqNum = data.m_senderSeqNum;
    m_targetSeqNum = data.m_targetSeqNum;

    for (const auto& [idx, msg] : data.m_messages) {
        m_messages.emplace(idx, m_dictionary->parse(m_settings, msg));
    }
}

int MemoryCache::getSenderSeqNum()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_senderSeqNum;
}

int MemoryCache::getTargetSeqNum()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_targetSeqNum;
}

void MemoryCache::setSenderSeqNum(int num)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_senderSeqNum = num;
    m_store.setSenderSeqNum(num);
}

void MemoryCache::setTargetSeqNum(int num)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_targetSeqNum = num;
    m_store.setTargetSeqNum(num);
}

int MemoryCache::nextSenderSeqNum()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    int next = getSenderSeqNum() + 1;
    setSenderSeqNum(next);
    return next;
}

int MemoryCache::nextTargetSeqNum()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    int next = getTargetSeqNum() + 1;
    setTargetSeqNum(next);
    return next;
}

void MemoryCache::cache(int seqnum, const Message& msg)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_messages[seqnum] = msg;
    m_store.store(seqnum, msg.toString(true));
}

void MemoryCache::getMessages(int begin, int end, MessageConsumer consumer)
{
    auto it = m_messages.lower_bound(begin);
    for (; it != m_messages.end() && (end == 0 || it->first <= end); ++it) {
        consumer(it->first, it->second);
    }
}

std::map<int, Message>& MemoryCache::getInboundQueue()
{
    return m_inboundQueue;
}
