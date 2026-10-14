#include "random_policy.h"
#include <algorithm>

RandomPolicy::RandomPolicy() : rng_(std::random_device{}()) {}

void RandomPolicy::onInsert(const std::string& key) {
    if (std::find(keys_.begin(), keys_.end(), key) == keys_.end())
        keys_.push_back(key);
}

void RandomPolicy::onAccess(const std::string&) {}

void RandomPolicy::onRemove(const std::string& key) {
    keys_.erase(std::remove(keys_.begin(), keys_.end(), key), keys_.end());
}

std::string RandomPolicy::selectVictim() const {
    if (keys_.empty()) return "";
    std::uniform_int_distribution<std::size_t> dist(0, keys_.size() - 1);
    return keys_[dist(rng_)];
}

void RandomPolicy::syncKeys(const std::vector<std::string>& keys) {
    keys_ = keys;
}
