#pragma once

#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <functional>

namespace Formidable {
namespace Server {
namespace Performance {

template<typename Key, typename Value>
class CacheManager {
public:
    using KeyValuePair = std::pair<Key, Value>;
    using ListIterator = typename std::list<KeyValuePair>::iterator;

    explicit CacheManager(size_t capacity) : m_capacity(capacity) {}

    void Put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_cacheMap.find(key);
        if (it != m_cacheMap.end()) {
            // 更新值并将节点移动到列表头部
            it->second->second = value;
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
            return;
        }

        // 插入新值
        if (m_cacheMap.size() >= m_capacity) {
            // 移除最久未使用的项
            auto last = m_lruList.end();
            last--;
            m_cacheMap.erase(last->first);
            m_lruList.pop_back();
        }

        m_lruList.emplace_front(key, value);
        m_cacheMap[key] = m_lruList.begin();
    }

    bool Get(const Key& key, Value& outValue) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_cacheMap.find(key);
        if (it == m_cacheMap.end()) {
            return false;
        }

        // 移动到头部
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        outValue = it->second->second;
        return true;
    }

    bool Exists(const Key& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cacheMap.find(key) != m_cacheMap.end();
    }
    
    void Remove(const Key& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cacheMap.find(key);
        if (it != m_cacheMap.end()) {
            m_lruList.erase(it->second);
            m_cacheMap.erase(it);
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cacheMap.clear();
        m_lruList.clear();
    }
    
    size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cacheMap.size();
    }

private:
    size_t m_capacity;
    std::list<KeyValuePair> m_lruList;
    std::unordered_map<Key, ListIterator> m_cacheMap;
    mutable std::mutex m_mutex;
};

} // namespace Performance
} // namespace Server
} // namespace Formidable
