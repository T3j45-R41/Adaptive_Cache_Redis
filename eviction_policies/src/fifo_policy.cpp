#include "fifo_policy.h"

void FIFOPolicy::onInsert(const std::string& key) {
    if (present_.insert(key).second) queue_.push_back(key);
}

void FIFOPolicy::onAccess(const std::string&) {
    // FIFO intentionally ignores access history.
}

void FIFOPolicy::onRemove(const std::string& key) {
    present_.erase(key);
}

std::string FIFOPolicy::selectVictim() const {
    for (const auto& key : queue_) {
        if (present_.find(key) != present_.end()) return key;
    }
    return "";
}

void FIFOPolicy::syncKeys(const std::vector<std::string>& keys) {
    std::unordered_set<std::string> wanted(keys.begin(), keys.end());
    for (const auto& key : keys) onInsert(key);

    for (auto it = present_.begin(); it != present_.end();) {
        if (wanted.find(*it) == wanted.end()) it = present_.erase(it);
        else ++it;
    }

    while (!queue_.empty() && present_.find(queue_.front()) == present_.end())
        queue_.pop_front();
}
