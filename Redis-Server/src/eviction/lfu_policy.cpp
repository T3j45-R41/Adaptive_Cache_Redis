#include "lfu_policy.h"
#include <limits>

void LFUPolicy::onInsert(const std::string& key) {
    // A re-insertion of a key that is already tracked must not reset its
    // counter -- that would let a hot key be demoted by a redundant write.
    if (meta_.find(key) != meta_.end()) return;
    ++logicalClock_;
    meta_[key] = Meta{1, logicalClock_};
}

void LFUPolicy::onAccess(const std::string& key) {
    auto it = meta_.find(key);
    if (it == meta_.end()) {
        onInsert(key);
        return;
    }
    ++logicalClock_;
    ++it->second.frequency;
    it->second.lastAccess = logicalClock_;
}

void LFUPolicy::onRemove(const std::string& key) {
    meta_.erase(key);
}

std::string LFUPolicy::selectVictim() const {
    if (meta_.empty()) return "";

    std::string victim;
    std::uint64_t bestFreq = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();

    for (const auto& entry : meta_) {
        const std::string& key = entry.first;
        const Meta& m = entry.second;
        if (m.frequency < bestFreq ||
            (m.frequency == bestFreq && m.lastAccess < oldest)) {
            victim = key;
            bestFreq = m.frequency;
            oldest = m.lastAccess;
        }
    }
    return victim;
}

void LFUPolicy::syncKeys(const std::vector<std::string>& keys) {
    const std::unordered_set<std::string> wanted(keys.begin(), keys.end());

    for (const auto& key : keys) {
        if (meta_.find(key) == meta_.end()) onInsert(key);
    }
    for (auto it = meta_.begin(); it != meta_.end();) {
        if (wanted.find(it->first) == wanted.end()) it = meta_.erase(it);
        else ++it;
    }
}