#ifndef FIFO_EVICTION_POLICY_H
#define FIFO_EVICTION_POLICY_H

#include "EvictionPolicy.h"
#include <list>
#include <unordered_map>
#include <mutex>

class FIFOEvictionPolicy : public EvictionPolicy {
public:
    FIFOEvictionPolicy(size_t capacity = 1000);
    ~FIFOEvictionPolicy() override = default;

    void onAccess(const std::string& key) override;
    void onInsert(const std::string& key) override;
    void onUpdate(const std::string& key) override;
    void onDelete(const std::string& key) override;

    std::string evict() override;
    std::string getName() const override { return "FIFO"; }
    EvictionStats getStats() const override;
    void recordHit() override;
    void recordMiss() override;
    void reset() override;

private:
    size_t capacity;
    std::list<std::string> fifo_queue; // front = oldest inserted, back = newest inserted
    std::unordered_map<std::string, std::list<std::string>::iterator> key_map;

    mutable std::mutex mutex_;
    uint64_t accesses_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
};

#endif // FIFO_EVICTION_POLICY_H
