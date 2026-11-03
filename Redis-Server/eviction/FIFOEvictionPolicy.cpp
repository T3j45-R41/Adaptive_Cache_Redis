#include "../include/FIFOEvictionPolicy.h"

FIFOEvictionPolicy::FIFOEvictionPolicy(size_t capacity) : capacity(capacity) {}

void FIFOEvictionPolicy::onAccess(const std::string& /*key*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    accesses_++;
    // In FIFO, access does not alter the queue position.
}

void FIFOEvictionPolicy::onInsert(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_map.find(key) == key_map.end()) {
        fifo_queue.push_back(key);
        key_map[key] = std::prev(fifo_queue.end());
    }
}

void FIFOEvictionPolicy::onUpdate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_map.find(key) == key_map.end()) {
        fifo_queue.push_back(key);
        key_map[key] = std::prev(fifo_queue.end());
    }
}

void FIFOEvictionPolicy::onDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        fifo_queue.erase(it->second);
        key_map.erase(it);
    }
}

std::string FIFOEvictionPolicy::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fifo_queue.empty()) return "";
    std::string victim = fifo_queue.front();
    fifo_queue.pop_front();
    key_map.erase(victim);
    evictions_++;
    return victim;
}

void FIFOEvictionPolicy::recordHit() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_++;
}

void FIFOEvictionPolicy::recordMiss() {
    std::lock_guard<std::mutex> lock(mutex_);
    misses_++;
}

EvictionStats FIFOEvictionPolicy::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EvictionStats stats;
    stats.policyName = "FIFO";
    stats.totalAccesses = accesses_;
    stats.totalHits = hits_;
    stats.totalMisses = misses_;
    stats.totalEvictions = evictions_;
    stats.currentSize = key_map.size();
    stats.capacity = capacity;
    return stats;
}

void FIFOEvictionPolicy::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    fifo_queue.clear();
    key_map.clear();
    accesses_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}
