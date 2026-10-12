#include "lru_policy.h"

void LRUPolicy::touch(const std::string& key) {
    auto it = pos_.find(key);
    if (it != pos_.end()) {
        order_.erase(it->second);
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
    return order_.empty() ? "" : order_.back();
}

void LRUPolicy::syncKeys(const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        if (pos_.find(key) == pos_.end()) touch(key);
    }
    for (auto it = pos_.begin(); it != pos_.end();) {
        bool keep = false;
        for (const auto& key : keys) {
            if (key == it->first) { keep = true; break; }
        }
        if (!keep) {
            order_.erase(it->second);
            it = pos_.erase(it);
        } else {
            ++it;
        }
    }
}
