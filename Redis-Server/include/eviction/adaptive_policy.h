#pragma once
#include "eviction_policy.h"
#include "lru_policy.h"
#include "lfu_policy.h"
#include <cstdint>
#include <string>

// Adaptive replacement by two-expert exponential weights.
//
// Both an LRU expert and an LFU expert are maintained in lockstep over the same
// resident key set, so exactly one key is ever really evicted. Before each
// decision each expert nominates its own candidate; the engine takes one of
// them and leaves the other as a shadow. If a shadow candidate is requested
// again shortly afterwards, that expert's ordering was wrong and it accrues
// loss; the weights are then updated multiplicatively against the lower-loss
// expert, with the accumulated loss decayed each round so recent evidence
// dominates and the weights cannot freeze at their initial values.
//
// This is a CacheUS-*inspired* controller: it shares CacheUS's idea of scoring
// alternatives in the shadow, but it has no sliding access window, no learned
// cost predictor, no gradient training and no ghost cache. It is reported as
// ADAPTIVE, not as a reproduction of CacheUS.
class AdaptivePolicy final : public EvictionPolicy {
public:
    AdaptivePolicy(double learningRate = 0.20, double lossDecay = 0.90);

    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    void onReplace(const std::string& key) override;
    std::string selectVictim() const override;
    void registerEvictionDecision(const std::string& selectedVictim) override;
    void syncKeys(const std::vector<std::string>& keys) override;
    std::string name() const override { return "ADAPTIVE"; }

    double lruWeight() const { return weightLRU_; }
    double lfuWeight() const { return weightLFU_; }
    double lruLoss() const { return lossLRU_; }
    double lfuLoss() const { return lossLFU_; }
    std::uint64_t decisions() const { return decisions_; }
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
    double lossDecay_;
    // Neither expert may be driven to zero: a dead expert can never recover
    // and the controller would stop adapting to a later workload shift.
    static constexpr double kMinWeight_ = 0.05;

    double weightLRU_{0.5};
    double weightLFU_{0.5};

    PendingCandidate pendingLRU_;
    PendingCandidate pendingLFU_;

    double lossLRU_{0.0};
    double lossLFU_{0.0};

    std::uint64_t decisions_{0};

    void normalizeWeights();
    void observeAccessLoss(const std::string& key);
};