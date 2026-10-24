#pragma once
#include "eviction_policy.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

// LFU: frequency counter per key, with recency as a tie-break so that keys of
// equal frequency are evicted oldest-first.
//
// This is the straightforward textbook LFU: victim selection is a linear scan
// over the counter table rather than an O(1) structure. That asymmetry against
// LRU's linked list is deliberate and documented in the benchmark notes --
// the policies are compared on cache quality, and LFU's selection cost is a
// known, separate concern.
class LFUPolicy final : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;
    std::string name() const override { return "LFU"; }

private:
    struct Meta {
        std::uint64_t frequency{0};
        std::uint64_t lastAccess{0};
    };

    std::unordered_map<std::string, Meta> meta_;
    std::uint64_t logicalClock_{0};
};