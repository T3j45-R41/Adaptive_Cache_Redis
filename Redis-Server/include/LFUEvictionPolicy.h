#ifndef LFU_EVICTION_POLICY_H
#define LFU_EVICTION_POLICY_H

#include "EvictionPolicy.h"
#include <list>
#include <unordered_map>
#include <map>
#include <mutex>

class LFUEvictionPolicy : public EvictionPolicy {
public:
    LFUEvictionPolicy(size_t capacity = 1000);
    ~LFUEvictionPolicy() override = default;

    void onAccess(const std::string& key) override;
    void onInsert(const std::string& key) override;
    void onUpdate(const std::string& key) override;
    void onDelete(const std::string& key) override;

    std::string evict() override;
    std::string getName() const override { return "LFU"; }
    EvictionStats getStats() const override;
    void recordHit() override;
    void recordMiss() override;
    void reset() override;

private:
    void touchKey(const std::string& key);

    size_t capacity;
    uint64_t min_freq = 0;

    std::unordered_map<std::string, uint64_t> key_freq;
    std::unordered_map<uint64_t, std::list<std::string>> freq_buckets; // freq -> list of keys (back = most recent, front = least recent)
    std::unordered_map<std::string, std::list<std::string>::iterator> key_iter;

    mutable std::mutex mutex_;
    uint64_t accesses_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
};

#endif // LFU_EVICTION_POLICY_H
