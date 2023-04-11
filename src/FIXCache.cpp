#include "FIXCache.h"

MemoryCache::MemoryCache(const SessionSettings& settings, std::shared_ptr<Dictionary> dictionary)
    : m_settings(settings)
    , m_dictionary(std::move(dictionary))
    , m_senderSeqNum(1)
    , m_targetSeqNum(1)
{

}

void MemoryCache::load(const SessionData& data)
{
    m_messages.clear();

    m_senderSeqNum = data.m_senderSeqNum;
    m_targetSeqNum = data.m_targetSeqNum;

    for (const auto& [idx, msg] : data.m_messages)
    {
        m_messages.emplace(idx, m_dictionary->parse(m_settings, msg));
    }
}