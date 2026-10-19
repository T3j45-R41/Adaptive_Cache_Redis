#pragma once
#include "eviction_policy.h"
#include <deque>
#include <string>
#include <unordered_set>

// FIFO: insertion order only. Accesses never reorder the queue, so a key that
// is read constantly can still be evicted while a stale key survives.
class FIFOPolicy final : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;
    std::string name() const override { return "FIFO"; }

private:
    std::deque<std::string> queue_;
    std::unordered_set<std::string> present_;

    // Drops leading tombstones left by explicit removes, keeping the queue
    // from growing without bound across a long-running benchmark.
    void trimFront();
};