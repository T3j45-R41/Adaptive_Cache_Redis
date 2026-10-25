#pragma once
#include "eviction_policy.h"
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

// Random: a uniformly random resident key, with no use of recency or
// frequency. Included as the null model -- any policy that cannot beat random
// under this workload is not learning anything about it.
//
// The membership set makes insert/remove O(1); the vector preserves a stable
// draw order so a given RNG stream replays the same victim sequence.
class RandomPolicy final : public EvictionPolicy {
public:
    RandomPolicy();
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;
    std::string name() const override { return "RANDOM"; }

private:
    std::vector<std::string> keys_;
    std::unordered_set<std::string> members_;
    mutable std::mt19937 rng_;
};