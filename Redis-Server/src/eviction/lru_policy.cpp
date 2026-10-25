#include "lru_policy.h"

void LRUPolicy::touch(const std::string& key) {
    auto it = pos_.find(key);
    if (it != pos_.end()) {
        order_.erase(it->second);
        pos_.erase(it);
    }
    order_.push_front(key);
    pos_[key] = order_.begin();
}

void LRUPolicy::onInsert(const std::string& key) { touch(key); }
void LRUPolicy::onAccess(const std::string& key) { touch(key); }

void LRUPolicy::onRemove(const std::string& key) {
    auto it = pos_.find(key);
    if (it == pos_.end()) return;
    order_.erase(it->second);
    pos_.erase(it);
}

std::string LRUPolicy::selectVictim() const {
    return order_.empty() ? std::string() : order_.back();
}

void LRUPolicy::syncKeys(const std::vector<std::string>& keys) {
    // Build the wanted set once: the previous version rescanned the whole key
    // list for every tracked key, making a resync quadratic.
    const std::unordered_set<std::string> wanted(keys.begin(), keys.end());

    for (const auto& key : keys) {
        if (pos_.find(key) == pos_.end()) touch(key);
    }
    for (auto it = pos_.begin(); it != pos_.end();) {
        if (wanted.find(it->first) == wanted.end()) {
            order_.erase(it->second);
            it = pos_.erase(it);
        } else {
            ++it;
        }
    }
}