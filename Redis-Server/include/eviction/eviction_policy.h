#pragma once
#include <string>
#include <vector>

class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;

    virtual void onInsert(const std::string& key) = 0;
    virtual void onAccess(const std::string& key) = 0;
    virtual void onRemove(const std::string& key) = 0;

    // Returns an existing cache key that should be evicted.
    virtual std::string selectVictim() const = 0;

    virtual void syncKeys(const std::vector<std::string>& keys) = 0;
};
