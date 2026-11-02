#ifndef LRU_EVICTION_POLICY_H
#define LRU_EVICTION_POLICY_H

#include "EvictionPolicy.h"
#include <list>
#include <unordered_map>
#include <mutex>

class LRUEvictionPolicy : public EvictionPolicy {
public:
    LRUEvictionPolicy(size_t capacity = 1000);
    ~LRUEvictionPolicy() override = default;

    void onAccess(const std::string& key) override;
    void onInsert(const std::string& key) override;
    void onUpdate(const std::string& key) override;
    void onDelete(const std::string& key) override;

    std::string evict() override;
    std::string getName() const override { return "LRU"; }
    EvictionStats getStats() const override;
    void recordHit() override;
    void recordMiss() override;
    void reset() override;

private:
    size_t capacity;
    std::list<std::string> lru_list; // front = most recent, back = least recent
    std::unordered_map<std::string, std::list<std::string>::iterator> key_map;

    mutable std::mutex mutex_;
    uint64_t accesses_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
};

#endif // LRU_EVICTION_POLICY_H
