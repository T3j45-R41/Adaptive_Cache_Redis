#ifndef RANDOM_EVICTION_POLICY_H
#define RANDOM_EVICTION_POLICY_H

#include "EvictionPolicy.h"
#include <vector>
#include <unordered_map>
#include <random>
#include <mutex>

class RandomEvictionPolicy : public EvictionPolicy {
public:
    RandomEvictionPolicy(size_t capacity = 1000, unsigned int seed = 42);
    ~RandomEvictionPolicy() override = default;

    void onAccess(const std::string& key) override;
    void onInsert(const std::string& key) override;
    void onUpdate(const std::string& key) override;
    void onDelete(const std::string& key) override;

    std::string evict() override;
    std::string getName() const override { return "RANDOM"; }
    EvictionStats getStats() const override;
    void recordHit() override;
    void recordMiss() override;
    void reset() override;

    void setSeed(unsigned int seed);

private:
    size_t capacity;
    unsigned int initial_seed;
    std::mt19937 rng;

    std::vector<std::string> keys_vec;
    std::unordered_map<std::string, size_t> key_idx;

    mutable std::mutex mutex_;
    uint64_t accesses_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
};

#endif // RANDOM_EVICTION_POLICY_H
