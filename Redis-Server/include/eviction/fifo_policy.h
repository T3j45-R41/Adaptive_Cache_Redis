#pragma once
#include "eviction_policy.h"
#include <deque>
#include <string>
#include <unordered_set>

class FIFOPolicy final : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;

private:
    std::deque<std::string> queue_;
    std::unordered_set<std::string> present_;
};
