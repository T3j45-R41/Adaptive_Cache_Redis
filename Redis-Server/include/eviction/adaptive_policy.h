#pragma once
#include "eviction_policy.h"
#include "lru_policy.h"
#include "lfu_policy.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class AdaptivePolicy final : public EvictionPolicy {
public:
    AdaptivePolicy(double learningRate = 0.20);

    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;

    // Call this after a real eviction. The selected victim is the actual
    // cache decision; the other expert's candidate remains in the cache.
    void registerEvictionDecision(const std::string& selectedVictim);

    double lruWeight() const { return weightLRU_; }
    double lfuWeight() const { return weightLFU_; }
    std::string preferredExpert() const {
        return weightLRU_ >= weightLFU_ ? "LRU" : "LFU";
    }

private:
    struct PendingCandidate {
        std::string key;
        bool active{false};
    };

    LRUPolicy lru_;
    LFUPolicy lfu_;

    double learningRate_;
    double weightLRU_{0.5};
    double weightLFU_{0.5};

    PendingCandidate pendingLRU_;
    PendingCandidate pendingLFU_;

    // Instantaneous shadow loss accumulated from reuse observations.
    double lossLRU_{0.0};
    double lossLFU_{0.0};

    // Number of adaptive decisions.
    std::uint64_t decisions_{0};

    void normalizeWeights();
    void observeAccessLoss(const std::string& key);
};

