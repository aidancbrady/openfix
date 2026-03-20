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
    const auto data = m_store.load();

    m_messages.clear();

    m_senderSeqNum.store(data.m_senderSeqNum, std::memory_order_release);
    m_targetSeqNum.store(data.m_targetSeqNum, std::memory_order_release);
    m_seqNumsDirty = false;
    m_pendingStoreSeqNums.clear();

    // Store wire strings directly — no need to parse on load
    for (auto& [idx, msg] : data.m_messages) {
        m_messages.emplace(idx, std::move(msg));
    }
}

void MemoryCache::reset()
{
    m_messages.clear();
    m_inboundQueue.clear();
    m_pendingStoreSeqNums.clear();

    m_senderSeqNum.store(0, std::memory_order_release);
    m_targetSeqNum.store(0, std::memory_order_release);
    m_seqNumsDirty = false;

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
    m_seqNumsDirty = true;
}

void MemoryCache::setTargetSeqNum(int num)
{
    m_targetSeqNum.store(num, std::memory_order_release);
    m_seqNumsDirty = true;
}

int MemoryCache::nextSenderSeqNum()
{
    const int next = m_senderSeqNum.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_seqNumsDirty = true;
    return next;
}

int MemoryCache::nextTargetSeqNum()
{
    const int next = m_targetSeqNum.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_seqNumsDirty = true;
    return next;
}

void MemoryCache::flushSeqNums()
{
    // batch write seq nums out of line
    if (!m_pendingStoreSeqNums.empty()) {
        for (const int seq : m_pendingStoreSeqNums) {
            const auto it = m_messages.find(seq);
            if (it != m_messages.end())
                m_store.store(seq, it->second);
        }
        m_pendingStoreSeqNums.clear();
    }

    if (!m_seqNumsDirty)
        return;
    m_seqNumsDirty = false;
    m_store.setSenderSeqNum(m_senderSeqNum.load(std::memory_order_acquire));
    m_store.setTargetSeqNum(m_targetSeqNum.load(std::memory_order_acquire));
}

void MemoryCache::cache(int seqnum, const std::string& wire)
{
    m_messages[seqnum] = wire;
    m_pendingStoreSeqNums.push_back(seqnum);
}

void MemoryCache::getMessages(int begin, int end, MessageConsumer consumer) const
{
    // we store messages out of order for efficiency; collect entries and order them here for replay
    std::vector<std::pair<int, const std::string*>> entries;
    for (const auto& [seqnum, wire] : m_messages) {
        if (seqnum >= begin && (end == 0 || seqnum <= end))
            entries.emplace_back(seqnum, &wire);
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [seqnum, wire] : entries) {
        consumer(seqnum, m_dictionary->parse(m_settings, *wire));
    }
}

std::map<int, Message>& MemoryCache::getInboundQueue()
{
    return m_inboundQueue;
}
