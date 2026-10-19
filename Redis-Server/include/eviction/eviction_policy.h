#pragma once
#include <string>
#include <vector>

// Pluggable cache replacement policy.
//
// The interface deliberately carries no capacity, no payload, and no clock:
// a policy is told which keys exist and in what order they are touched, and
// nominates one victim when the owner needs space. Keeping the policy free of
// storage details is what lets the same objects serve the server, the unit
// tests, and the offline workload replay.
class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;

    virtual void onInsert(const std::string& key) = 0;
    virtual void onAccess(const std::string& key) = 0;
    virtual void onRemove(const std::string& key) = 0;

    // Rewriting an existing key's value is not a read and not a re-insertion:
    // it must not reset recency or frequency. Policies with no such concept
    // need no override.
    virtual void onReplace(const std::string& /*key*/) {}

    // Returns an existing cache key that should be evicted, or "" if the
    // policy sees no candidate.
    virtual std::string selectVictim() const = 0;

    // Called by the owner *after* a victim has actually been removed, so a
    // policy that evaluates alternatives in the shadow can learn from the
    // choice it did not get. Stateless policies ignore this.
    virtual void registerEvictionDecision(const std::string& /*selectedVictim*/) {}

    // Bulk resynchronisation after an external change (snapshot load, FLUSHALL).
    virtual void syncKeys(const std::vector<std::string>& keys) = 0;

    virtual std::string name() const = 0;
};