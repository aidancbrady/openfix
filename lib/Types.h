#pragma once

#include <ankerl/unordered_dense.h>
#include <concurrentqueue.h>

template <typename K, typename V, typename Hash = ankerl::unordered_dense::hash<K>>
using HashMapT = ankerl::unordered_dense::map<K, V, Hash>;

template <typename K, typename Hash = ankerl::unordered_dense::hash<K>>
using HashSetT = ankerl::unordered_dense::set<K, Hash>;

template <typename T>
using LockFreeQueueT = moodycamel::ConcurrentQueue<T>;
