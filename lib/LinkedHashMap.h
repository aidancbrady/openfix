#pragma once

#include <cstddef>
#include <utility>
#include <vector>

// Insertion-ordered map backed by a flat std::vector.
// Optimized for small N (typical FIX messages have ~15 fields):
//   - Cache-friendly contiguous storage
//   - Zero per-element heap allocations
//   - Linear scan for find() beats hash lookup when N < ~32
template <typename K, typename V>
class LinkedHashMap
{
public:
    using value_type = std::pair<K, V>;
    using storage_type = std::vector<value_type>;
    using iterator = typename storage_type::iterator;
    using const_iterator = typename storage_type::const_iterator;

    LinkedHashMap() = default;

    iterator find(const K& key)
    {
        for (auto it = m_data.begin(); it != m_data.end(); ++it)
            if (it->first == key)
                return it;
        return m_data.end();
    }

    const_iterator find(const K& key) const
    {
        for (auto it = m_data.begin(); it != m_data.end(); ++it)
            if (it->first == key)
                return it;
        return m_data.end();
    }

    V& operator[](const K& key)
    {
        auto it = find(key);
        if (it != m_data.end())
            return it->second;
        m_data.emplace_back(key, V{});
        return m_data.back().second;
    }

    std::pair<iterator, bool> insert(const value_type& value)
    {
        auto it = find(value.first);
        if (it != m_data.end())
            return {it, false};
        m_data.push_back(value);
        return {m_data.end() - 1, true};
    }

    iterator insert(iterator pos, const value_type& value)
    {
        if (find(value.first) != m_data.end())
            return m_data.end();
        return m_data.insert(pos, value);
    }

    void erase(iterator it)
    {
        if (it != m_data.end())
            m_data.erase(it);
    }

    bool erase(const K& key)
    {
        auto it = find(key);
        if (it == m_data.end())
            return false;
        m_data.erase(it);
        return true;
    }

    bool empty() const { return m_data.empty(); }
    size_t size() const { return m_data.size(); }

    iterator begin() { return m_data.begin(); }
    iterator end() { return m_data.end(); }
    const_iterator begin() const { return m_data.begin(); }
    const_iterator end() const { return m_data.end(); }

private:
    storage_type m_data;
};
