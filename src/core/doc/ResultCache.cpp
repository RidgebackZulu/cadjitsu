#include "doc/ResultCache.h"

#include <algorithm>
#include <vector>

namespace cad {

std::optional<ResultCache::Entry> ResultCache::find(uint64_t key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_map.find(key);
    if(it == m_map.end()) return std::nullopt;
    it->second.lastUse = ++m_clock;
    return it->second.entry;
}

void ResultCache::insert(uint64_t key, Entry entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map[key] = Slot{std::move(entry), ++m_clock};
    evict();
}

void ResultCache::setPinned(std::set<uint64_t> keys) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pinned = std::move(keys);
    evict();
}

void ResultCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.clear();
}

size_t ResultCache::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_map.size();
}

void ResultCache::evict() {
    if(m_map.size() <= m_capacity) return;
    std::vector<std::pair<uint64_t, uint64_t>> candidates; // lastUse, key
    for(const auto &kv : m_map)
        if(!m_pinned.count(kv.first)) candidates.push_back({kv.second.lastUse, kv.first});
    std::sort(candidates.begin(), candidates.end());
    size_t excess = m_map.size() - m_capacity;
    for(size_t i = 0; i < candidates.size() && excess > 0; ++i, --excess) m_map.erase(candidates[i].second);
}

} // namespace cad
