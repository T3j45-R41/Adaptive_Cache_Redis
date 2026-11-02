#ifndef ADAPTIVE_EVICTION_POLICY_H
#define ADAPTIVE_EVICTION_POLICY_H

#include "EvictionPolicy.h"
#include "LRUEvictionPolicy.h"
#include "LFUEvictionPolicy.h"
#include <deque>
#include <unordered_set>
#include <random>
#include <mutex>
#include <cmath>

class AdaptiveEvictionPolicy : public EvictionPolicy {
public:
    AdaptiveEvictionPolicy(size_t capacity = 1000, double learningRate = 0.05, unsigned int seed = 123);
    ~AdaptiveEvictionPolicy() override = default;

    void onAccess(const std::string& key) override;
    void onInsert(const std::string& key) override;
    void onUpdate(const std::string& key) override;
    void onDelete(const std::string& key) override;

    std::string evict() override;
    std::string getName() const override { return "ADAPTIVE"; }
    EvictionStats getStats() const override;
    void recordHit() override;
    void recordMiss() override;
    void reset() override;

    // Regret update on miss when a key is found in shadow histories
    void onCacheMissFeedback(const std::string& missedKey);

    // Getters for adaptive internal states
    double getExpert1Weight() const;
    double getExpert2Weight() const;
    std::string getCurrentActiveExpert() const;

private:
    void addToShadow(std::deque<std::string>& shadowQueue,
                     std::unordered_set<std::string>& shadowSet,
                     const std::string& key);

    size_t capacity;
    double learningRate;
    unsigned int initial_seed;
    std::mt19937 rng;

    // Experts
    LRUEvictionPolicy expertLRU; // Expert 1 (Recency)
    LFUEvictionPolicy expertLFU; // Expert 2 (Frequency)

    // Weights
    double w1 = 0.5; // LRU weight
    double w2 = 0.5; // LFU weight

    // Cumulative Regret
    double cumulativeRegret1 = 0.0;
    double cumulativeRegret2 = 0.0;

    // Selection Counters
    uint64_t expert1Selections = 0;
    uint64_t expert2Selections = 0;
    std::string lastSelectedExpert = "LRU";

    // Shadow Histories (bounded by capacity)
    std::deque<std::string> shadow1Queue; // LRU shadow queue
    std::unordered_set<std::string> shadow1Set;

    std::deque<std::string> shadow2Queue; // LFU shadow queue
    std::unordered_set<std::string> shadow2Set;

    mutable std::mutex mutex_;
    uint64_t accesses_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t evictions_ = 0;
};

#endif // ADAPTIVE_EVICTION_POLICY_H
