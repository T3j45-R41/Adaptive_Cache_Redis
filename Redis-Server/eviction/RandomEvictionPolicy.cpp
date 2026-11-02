#include "../include/RandomEvictionPolicy.h"

RandomEvictionPolicy::RandomEvictionPolicy(size_t capacity, unsigned int seed)
    : capacity(capacity), initial_seed(seed), rng(seed) {}

void RandomEvictionPolicy::setSeed(unsigned int seed) {
    std::lock_guard<std::mutex> lock(mutex_);
    initial_seed = seed;
    rng.seed(seed);
}

void RandomEvictionPolicy::onAccess(const std::string& /*key*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    accesses_++;
}

void RandomEvictionPolicy::onInsert(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_idx.find(key) == key_idx.end()) {
        keys_vec.push_back(key);
        key_idx[key] = keys_vec.size() - 1;
    }
}

void RandomEvictionPolicy::onUpdate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_idx.find(key) == key_idx.end()) {
        keys_vec.push_back(key);
        key_idx[key] = keys_vec.size() - 1;
    }
}

void RandomEvictionPolicy::onDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = key_idx.find(key);
    if (it != key_idx.end()) {
        size_t idx = it->second;
        size_t last_idx = keys_vec.size() - 1;
        if (idx != last_idx) {
            std::string last_key = keys_vec[last_idx];
            keys_vec[idx] = last_key;
            key_idx[last_key] = idx;
        }
        keys_vec.pop_back();
        key_idx.erase(it);
    }
}

std::string RandomEvictionPolicy::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keys_vec.empty()) return "";

    std::uniform_int_distribution<size_t> dist(0, keys_vec.size() - 1);
    size_t victim_idx = dist(rng);
    std::string victim = keys_vec[victim_idx];

    // Swap with back and pop
    size_t last_idx = keys_vec.size() - 1;
    if (victim_idx != last_idx) {
        std::string last_key = keys_vec[last_idx];
        keys_vec[victim_idx] = last_key;
        key_idx[last_key] = victim_idx;
    }
    keys_vec.pop_back();
    key_idx.erase(victim);

    evictions_++;
    return victim;
}

void RandomEvictionPolicy::recordHit() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_++;
}

void RandomEvictionPolicy::recordMiss() {
    std::lock_guard<std::mutex> lock(mutex_);
    misses_++;
}

EvictionStats RandomEvictionPolicy::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EvictionStats stats;
    stats.policyName = "RANDOM";
    stats.totalAccesses = accesses_;
    stats.totalHits = hits_;
    stats.totalMisses = misses_;
    stats.totalEvictions = evictions_;
    stats.currentSize = keys_vec.size();
    stats.capacity = capacity;
    return stats;
}

void RandomEvictionPolicy::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_vec.clear();
    key_idx.clear();
    rng.seed(initial_seed);
    accesses_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}
