#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// insertion-ordered map backed by a flat std::vector
// for integral keys, maintains a flat index for O(1) find/has/operator[]
template <typename K, typename V>
class LinkedHashMap
{
    static constexpr bool Indexed = std::is_integral_v<K>;

public:
    using value_type = std::pair<K, V>;
    using storage_type = std::vector<value_type>;
    using iterator = typename storage_type::iterator;
    using const_iterator = typename storage_type::const_iterator;

    LinkedHashMap() = default;

    iterator find(const K& key)
    {
        if constexpr (Indexed) {
            auto k = static_cast<size_t>(key);
            if (k < m_index.size()) {
                auto idx = m_index[k];
                if (idx >= 0)
                    return m_data.begin() + idx;
            }
            return m_data.end();
        } else {
            for (auto it = m_data.begin(); it != m_data.end(); ++it)
                if (it->first == key)
                    return it;
            return m_data.end();
        }
    }

    const_iterator find(const K& key) const
    {
        if constexpr (Indexed) {
            auto k = static_cast<size_t>(key);
            if (k < m_index.size()) {
                auto idx = m_index[k];
                if (idx >= 0)
                    return m_data.begin() + idx;
            }
            return m_data.end();
        } else {
            for (auto it = m_data.begin(); it != m_data.end(); ++it)
                if (it->first == key)
                    return it;
            return m_data.end();
        }
    }

    V& operator[](const K& key)
    {
        auto it = find(key);
        if (it != m_data.end())
            return it->second;
        if constexpr (Indexed) {
            growIndex(key);
            m_index[static_cast<size_t>(key)] = static_cast<int32_t>(m_data.size());
        }
        m_data.emplace_back(key, V{});
        return m_data.back().second;
    }

    std::pair<iterator, bool> insert(const value_type& value)
    {
        auto it = find(value.first);
        if (it != m_data.end())
            return {it, false};
        if constexpr (Indexed) {
            growIndex(value.first);
            m_index[static_cast<size_t>(value.first)] = static_cast<int32_t>(m_data.size());
        }
        m_data.push_back(value);
        return {m_data.end() - 1, true};
    }

    iterator insert(iterator pos, const value_type& value)
    {
        if (find(value.first) != m_data.end())
            return m_data.end();
        auto result = m_data.insert(pos, value);
        if constexpr (Indexed)
            rebuildIndex();
        return result;
    }

    void erase(iterator it)
    {
        if (it != m_data.end()) {
            m_data.erase(it);
            if constexpr (Indexed)
                rebuildIndex();
        }
    }

    bool erase(const K& key)
    {
        auto it = find(key);
        if (it == m_data.end())
            return false;
        m_data.erase(it);
        if constexpr (Indexed)
            rebuildIndex();
        return true;
    }

    void reserve(size_t n) { m_data.reserve(n); }

    // Pre-allocate the flat index to avoid repeated reallocations
    // when inserting keys up to maxKey. Only available for integral keys.
    void reserveIndex(size_t maxKey)
        requires Indexed
    {
        if (maxKey >= m_index.size())
            m_index.resize(maxKey + 1, -1);
    }

    bool empty() const { return m_data.empty(); }
    size_t size() const { return m_data.size(); }

    iterator begin() { return m_data.begin(); }
    iterator end() { return m_data.end(); }
    const_iterator begin() const { return m_data.begin(); }
    const_iterator end() const { return m_data.end(); }

private:
    void growIndex(const K& key)
        requires Indexed
    {
        auto k = static_cast<size_t>(key);
        if (k >= m_index.size())
            m_index.resize(k + 1, -1);
    }

    void rebuildIndex()
        requires Indexed
    {
        std::fill(m_index.begin(), m_index.end(), -1);
        for (size_t i = 0; i < m_data.size(); ++i) {
            auto k = static_cast<size_t>(m_data[i].first);
            if (k >= m_index.size())
                m_index.resize(k + 1, -1);
            m_index[k] = static_cast<int32_t>(i);
        }
    }

    storage_type m_data;

    // Flat index: maps key -> position in m_data. Only present for integral keys.
    // Gives O(1) find/has/operator[] for FIX tag lookups.
    struct Empty {};
    [[no_unique_address]]
    std::conditional_t<Indexed, std::vector<int32_t>, Empty> m_index;
};
