#pragma once

#include "base/Status.h"
#include "doc/ModelState.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

namespace cad {

// Content-addressed cache of computed model states. A timeline position's key
// hashes the previous key with the feature's canonical JSON and evaluated
// parameters, so scrubbing, undo and un-suppress hit the cache.
class ResultCache {
public:
    struct Entry {
        StatePtr state;
        Status status;
    };

    explicit ResultCache(size_t capacity = 256) : m_capacity(capacity) {}

    std::optional<Entry> find(uint64_t key);
    void insert(uint64_t key, Entry entry);
    // Keys of the live timeline are never evicted.
    void setPinned(std::set<uint64_t> keys);
    void clear();
    size_t size() const;

private:
    void evict();

    struct Slot {
        Entry entry;
        uint64_t lastUse = 0;
    };
    mutable std::mutex m_mutex;
    size_t m_capacity;
    uint64_t m_clock = 0;
    std::unordered_map<uint64_t, Slot> m_map;
    std::set<uint64_t> m_pinned;
};

} // namespace cad
