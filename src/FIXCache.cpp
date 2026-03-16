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

    m_senderSeqNum.store(data.m_senderSeqNum, std::memory_order_release);
    m_targetSeqNum.store(data.m_targetSeqNum, std::memory_order_release);

    // Store wire strings directly — no need to parse on load
    for (auto& [idx, msg] : data.m_messages) {
        m_messages.emplace(idx, std::move(msg));
    }
}

void MemoryCache::reset()
{
    m_messages.clear();
    m_inboundQueue.clear();

    m_senderSeqNum.store(0, std::memory_order_release);
    m_targetSeqNum.store(0, std::memory_order_release);

    m_store.reset();
}

int MemoryCache::getSenderSeqNum() const
{
    return m_senderSeqNum.load(std::memory_order_acquire);
}

int MemoryCache::getTargetSeqNum() const
{
    return m_targetSeqNum.load(std::memory_order_acquire);
}

void MemoryCache::setSenderSeqNum(int num)
{
    m_senderSeqNum.store(num, std::memory_order_release);
    m_store.setSenderSeqNum(num);
}

void MemoryCache::setTargetSeqNum(int num)
{
    m_targetSeqNum.store(num, std::memory_order_release);
    m_store.setTargetSeqNum(num);
}

int MemoryCache::nextSenderSeqNum()
{
    int next = m_senderSeqNum.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_store.setSenderSeqNum(next);
    return next;
}

int MemoryCache::nextTargetSeqNum()
{
    int next = m_targetSeqNum.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_store.setTargetSeqNum(next);
    return next;
}

void MemoryCache::cache(int seqnum, const std::string& wire)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_messages[seqnum] = wire;
    m_store.store(seqnum, wire);
}

void MemoryCache::getMessages(int begin, int end, MessageConsumer consumer) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_messages.lower_bound(begin);
    for (; it != m_messages.end() && (end == 0 || it->first <= end); ++it) {
        consumer(it->first, m_dictionary->parse(m_settings, it->second));
    }
}

std::map<int, Message>& MemoryCache::getInboundQueue()
{
    return m_inboundQueue;
}
