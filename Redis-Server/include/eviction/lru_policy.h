#pragma once
#include "eviction_policy.h"
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Classic LRU: a doubly linked list ordered most-recently-used at the front,
// plus a hash map from key to its list iterator so the splice is O(1).
class LRUPolicy final : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;
    std::string name() const override { return "LRU"; }

private:
    std::list<std::string> order_; // front = MRU, back = LRU
    std::unordered_map<std::string, std::list<std::string>::iterator> pos_;

    void touch(const std::string& key);
};