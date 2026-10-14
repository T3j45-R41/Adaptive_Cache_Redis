#pragma once
#include "eviction_policy.h"
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

class RandomPolicy final : public EvictionPolicy {
public:
    RandomPolicy();
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;

private:
    std::vector<std::string> keys_;
    mutable std::mt19937 rng_;
};
