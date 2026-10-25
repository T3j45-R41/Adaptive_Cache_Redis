#pragma once
#include "cache_entry.h"
#include "eviction_metrics.h"
#include "eviction_policy.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Owns the active replacement policy, the configured capacity, and the
// counters that make cache behaviour measurable.
//
// This is the seam between the storage engine and the policy library: the
// database calls in on insert/access/remove and asks for a victim when it is
// over capacity, and never touches a concrete policy type. Not internally
// synchronised -- the owning database serialises access under its own lock.
class EvictionEngine {
public:
    EvictionEngine();
    ~EvictionEngine();
    EvictionEngine(const EvictionEngine&) = delete;
    EvictionEngine& operator=(const EvictionEngine&) = delete;

    // Policy names are matched case-insensitively. "CACHEUS" is accepted as a
    // legacy alias for "ADAPTIVE" because earlier benchmark scripts and docs
    // still send it.
    static bool isKnownPolicy(const std::string& name);
    static std::string canonicalPolicyName(const std::string& name);
    static std::vector<std::string> policyNames();

    bool setPolicy(const std::string& name);
    const std::string& policyName() const { return policyName_; }
    const EvictionPolicy& policy() const { return *policy_; }

    // 0 disables capacity enforcement entirely.
    void setMaxKeys(std::size_t keys);
    std::size_t maxKeys() const { return maxKeys_; }
    bool capacityLimited() const { return maxKeys_ > 0; }

    void onInsert(const std::string& key);
    void onAccess(const std::string& key);
    void onReplace(const std::string& key);
    void onRemove(const std::string& key);
    void onCapacityEnforced();
    std::string selectVictim() const;
    // Must be called *before* the victim is removed, while both experts still
    // agree on the resident set and can each nominate a shadow candidate.
    void registerEvictionDecisionFeedback(const std::string& victim);
    void resync(const std::vector<std::string>& keys);

    void recordHit();
    void recordMiss();
    void recordEviction(bool dirty);
    void recordExpiration();
    void recordExplicitDelete();
    void resetStats();

    const EvictionMetrics& metrics() const { return metrics_; }

    // Extra introspection for the adaptive controller; empty for static policies.
    std::string policyDetail() const;

private:
    std::unique_ptr<EvictionPolicy> policy_;
    std::string policyName_;
    std::size_t maxKeys_{0};
    EvictionMetrics metrics_;

    void bumpPolicySelection();
};