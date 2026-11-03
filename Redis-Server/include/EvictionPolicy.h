#ifndef EVICTION_POLICY_H
#define EVICTION_POLICY_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

enum class PolicyType {
    LRU,
    LFU,
    FIFO,
    RANDOM,
    ADAPTIVE
};

inline std::string policyTypeToString(PolicyType type) {
    switch (type) {
        case PolicyType::LRU: return "LRU";
        case PolicyType::LFU: return "LFU";
        case PolicyType::FIFO: return "FIFO";
        case PolicyType::RANDOM: return "RANDOM";
        case PolicyType::ADAPTIVE: return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

inline PolicyType stringToPolicyType(const std::string& str) {
    if (str == "LRU" || str == "lru") return PolicyType::LRU;
    if (str == "LFU" || str == "lfu") return PolicyType::LFU;
    if (str == "FIFO" || str == "fifo") return PolicyType::FIFO;
    if (str == "RANDOM" || str == "random") return PolicyType::RANDOM;
    if (str == "ADAPTIVE" || str == "adaptive" || str == "CACHEUS" || str == "cacheus") return PolicyType::ADAPTIVE;
    return PolicyType::LRU;
}

struct EvictionStats {
    std::string policyName = "LRU";
    uint64_t totalAccesses = 0;
    uint64_t totalHits = 0;
    uint64_t totalMisses = 0;
    uint64_t totalEvictions = 0;
    size_t currentSize = 0;
    size_t capacity = 1000;

    // Adaptive policy metrics
    double expert1Weight = 0.5; // LRU/Recency weight
    double expert2Weight = 0.5; // LFU/Frequency weight
    double cumulativeRegret1 = 0.0;
    double cumulativeRegret2 = 0.0;
    uint64_t expert1Selections = 0;
    uint64_t expert2Selections = 0;
    std::string currentActiveExpert = "LRU";
    double learningRate = 0.05;
};

class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;

    // Lifecycle events triggered by cache operations
    virtual void onAccess(const std::string& key) = 0;
    virtual void onInsert(const std::string& key) = 0;
    virtual void onUpdate(const std::string& key) = 0;
    virtual void onDelete(const std::string& key) = 0;

    // Returns the key selected for eviction, or empty string if empty
    virtual std::string evict() = 0;

    virtual std::string getName() const = 0;
    virtual EvictionStats getStats() const = 0;
    virtual void recordHit() = 0;
    virtual void recordMiss() = 0;
    virtual void reset() = 0;
};

#endif // EVICTION_POLICY_H
