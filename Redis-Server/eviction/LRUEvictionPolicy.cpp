#include "../include/LRUEvictionPolicy.h"

LRUEvictionPolicy::LRUEvictionPolicy(size_t capacity) : capacity(capacity) {}

void LRUEvictionPolicy::onAccess(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    accesses_++;
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        lru_list.splice(lru_list.begin(), lru_list, it->second);
    }
}

void LRUEvictionPolicy::onInsert(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        lru_list.splice(lru_list.begin(), lru_list, it->second);
    } else {
        lru_list.push_front(key);
        key_map[key] = lru_list.begin();
    }
}

void LRUEvictionPolicy::onUpdate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        lru_list.splice(lru_list.begin(), lru_list, it->second);
    } else {
        lru_list.push_front(key);
        key_map[key] = lru_list.begin();
    }
}

void LRUEvictionPolicy::onDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        lru_list.erase(it->second);
        key_map.erase(it);
    }
}

std::string LRUEvictionPolicy::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lru_list.empty()) return "";
    std::string victim = lru_list.back();
    lru_list.pop_back();
    key_map.erase(victim);
    evictions_++;
    return victim;
}

void LRUEvictionPolicy::recordHit() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_++;
}

void LRUEvictionPolicy::recordMiss() {
    std::lock_guard<std::mutex> lock(mutex_);
    misses_++;
}

EvictionStats LRUEvictionPolicy::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EvictionStats stats;
    stats.policyName = "LRU";
    stats.totalAccesses = accesses_;
    stats.totalHits = hits_;
    stats.totalMisses = misses_;
    stats.totalEvictions = evictions_;
    stats.currentSize = key_map.size();
    stats.capacity = capacity;
    return stats;
}

void LRUEvictionPolicy::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_list.clear();
    key_map.clear();
    accesses_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}
