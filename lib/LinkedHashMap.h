#pragma once

#include <list>
#include <unordered_map>

template<typename K, typename V>
class LinkedHashMap
{
    using value_type = std::pair<K, V>;
    using list_type = std::list<value_type>;
    using list_iterator = typename list_type::iterator;

    using map_type = std::unordered_map<K, list_iterator>;

    template<typename IteratorType>
    class BaseIterator
    {
        using value_type = typename std::iterator_traits<IteratorType>::value_type;
        using reference = typename std::iterator_traits<IteratorType>::reference;
        using pointer = typename std::iterator_traits<IteratorType>::pointer;

    private:
        IteratorType m_it;

    public:
        explicit BaseIterator(IteratorType it) : m_it(it) {}

        BaseIterator& operator++()
        {
            ++m_it;
            return *this;
        }

        bool operator==(const BaseIterator& other) const 
        {
            return m_it == other.m_it;
        }

        bool operator!=(const BaseIterator& other) const 
        {
            return m_it != other.m_it;
        }

        reference operator*() const 
        {
            return *m_it;
        }

        pointer operator->() const 
        {
            return &(*m_it);
        }
    };

    using iterator = BaseIterator<typename list_type::iterator>;
    using const_iterator = BaseIterator<typename list_type::const_iterator>;

public:
    LinkedHashMap() = default;
    
    LinkedHashMap(const LinkedHashMap& other) 
    {
        for (const auto& pair : other.m_list) 
        {
            auto list_it = m_list.emplace_back(pair);
            m_map[pair.first] = --m_list.end();
        }
    }

    LinkedHashMap(LinkedHashMap&& other)
    {
        m_list = std::move(other.m_list);
        m_map = std::move(other.m_map);
    }

    LinkedHashMap& operator=(const LinkedHashMap& other) 
    {
        if (this != &other) 
        {
            m_list.clear();
            m_map.clear();

            for (const auto& pair : other.m_list) 
            {
                auto list_it = m_list.emplace_back(pair);
                m_map[pair.first] = --m_list.end();
            }
        }
        return *this;
    }

    LinkedHashMap& operator=(LinkedHashMap&& other) 
    {
        if (this != &other)
        {
            m_list = std::move(other.m_list);
            m_map = std::move(other.m_map);
        }
        return *this;
    }

    iterator find(const K& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return end();
        return iterator(it->second);
    }

    const_iterator find(const K& key) const
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return end();
        return const_iterator(it->second);
    }

    V& operator[](const K& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end()) 
        {
            m_list.emplace_back(key, V{});
            it = m_map.emplace(key, --m_list.end()).first;
        }
        return it->second->second;
    }

    std::pair<iterator, bool> insert(const value_type& value) 
    {
        auto it = m_map.find(value.first);
        if (it != m_map.end())
            return {iterator(it->second), false};
        auto list_it = m_list.emplace(m_list.end(), value);
        m_map[value.first] = list_it;
        return {iterator(list_it), true};
    }

    void erase(iterator it)
    {
        auto map_it = m_map.find(it->first);
        if (map_it != m_map.end())
        {
            m_list.erase(map_it->second);
            m_map.erase(map_it);
        }
    }

    bool erase(const K& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return false;
        m_list.erase(it->second);
        m_map.erase(it);
        return true;
    }

    bool empty() const
    {
        return m_list.empty();
    }

    size_t size() const
    {
        return m_list.size();
    }

    iterator begin()
    {
        return iterator(m_list.begin());
    }

    iterator end()
    {
        return iterator(m_list.end());
    }

    const_iterator begin() const
    {
        return const_iterator(m_list.begin());
    }

    const_iterator end() const
    {
        return const_iterator(m_list.end());
    }

private:
    list_type m_list;
    map_type m_map;
};
