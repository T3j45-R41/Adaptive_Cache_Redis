#pragma once
#include "eviction_policy.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

class LFUPolicy final : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override;
    void onAccess(const std::string& key) override;
    void onRemove(const std::string& key) override;
    std::string selectVictim() const override;
    void syncKeys(const std::vector<std::string>& keys) override;

private:
    struct Meta {
        std::uint64_t frequency{0};
        std::uint64_t lastAccess{0};
    };

    std::unordered_map<std::string, Meta> meta_;
    std::uint64_t logicalClock_{0};
};
