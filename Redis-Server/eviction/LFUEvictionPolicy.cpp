#include "../include/LFUEvictionPolicy.h"

LFUEvictionPolicy::LFUEvictionPolicy(size_t capacity) : capacity(capacity), min_freq(0) {}

void LFUEvictionPolicy::touchKey(const std::string& key) {
    auto fIt = key_freq.find(key);
    if (fIt == key_freq.end()) return;

    uint64_t old_freq = fIt->second;
    uint64_t new_freq = old_freq + 1;
    fIt->second = new_freq;

    // Erase from old frequency bucket
    auto itIt = key_iter.find(key);
    if (itIt != key_iter.end()) {
        freq_buckets[old_freq].erase(itIt->second);
        if (freq_buckets[old_freq].empty()) {
            freq_buckets.erase(old_freq);
            if (min_freq == old_freq) {
                min_freq = new_freq;
            }
        }
    }

    // Add to new frequency bucket (back = most recent for recency tie-breaking)
    freq_buckets[new_freq].push_back(key);
    key_iter[key] = std::prev(freq_buckets[new_freq].end());
}

void LFUEvictionPolicy::onAccess(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    accesses_++;
    touchKey(key);
}

void LFUEvictionPolicy::onInsert(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_freq.find(key) != key_freq.end()) {
        touchKey(key);
        return;
    }

    uint64_t freq = 1;
    key_freq[key] = freq;
    freq_buckets[freq].push_back(key);
    key_iter[key] = std::prev(freq_buckets[freq].end());
    min_freq = 1;
}

void LFUEvictionPolicy::onUpdate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_freq.find(key) != key_freq.end()) {
        touchKey(key);
    } else {
        uint64_t freq = 1;
        key_freq[key] = freq;
        freq_buckets[freq].push_back(key);
        key_iter[key] = std::prev(freq_buckets[freq].end());
        min_freq = 1;
    }
}

void LFUEvictionPolicy::onDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto fIt = key_freq.find(key);
    if (fIt == key_freq.end()) return;

    uint64_t freq = fIt->second;
    auto itIt = key_iter.find(key);
    if (itIt != key_iter.end()) {
        freq_buckets[freq].erase(itIt->second);
        if (freq_buckets[freq].empty()) {
            freq_buckets.erase(freq);
            if (min_freq == freq) {
                if (key_freq.size() <= 1) {
                    min_freq = 0;
                } else {
                    min_freq = 0;
                    for (const auto& kv : freq_buckets) {
                        if (min_freq == 0 || kv.first < min_freq) {
                            min_freq = kv.first;
                        }
                    }
                }
            }
        }
        key_iter.erase(itIt);
    }
    key_freq.erase(fIt);
}

std::string LFUEvictionPolicy::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_freq.empty() || min_freq == 0) return "";

    auto bucketIt = freq_buckets.find(min_freq);
    if (bucketIt == freq_buckets.end() || bucketIt->second.empty()) {
        // Re-calculate min_freq if inconsistent
        min_freq = 0;
        for (const auto& kv : freq_buckets) {
            if (!kv.second.empty() && (min_freq == 0 || kv.first < min_freq)) {
                min_freq = kv.first;
            }
        }
        if (min_freq == 0) return "";
        bucketIt = freq_buckets.find(min_freq);
    }

    // Tie-breaker: front of the list is the least recently accessed of this frequency
    std::string victim = bucketIt->second.front();
    bucketIt->second.pop_front();
    if (bucketIt->second.empty()) {
        freq_buckets.erase(bucketIt);
        // Find next minimum frequency
        min_freq = 0;
        for (const auto& kv : freq_buckets) {
            if (!kv.second.empty() && (min_freq == 0 || kv.first < min_freq)) {
                min_freq = kv.first;
            }
        }
    }

    key_iter.erase(victim);
    key_freq.erase(victim);
    evictions_++;
    return victim;
}

void LFUEvictionPolicy::recordHit() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_++;
}

void LFUEvictionPolicy::recordMiss() {
    std::lock_guard<std::mutex> lock(mutex_);
    misses_++;
}

EvictionStats LFUEvictionPolicy::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EvictionStats stats;
    stats.policyName = "LFU";
    stats.totalAccesses = accesses_;
    stats.totalHits = hits_;
    stats.totalMisses = misses_;
    stats.totalEvictions = evictions_;
    stats.currentSize = key_freq.size();
    stats.capacity = capacity;
    return stats;
}

void LFUEvictionPolicy::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    key_freq.clear();
    freq_buckets.clear();
    key_iter.clear();
    min_freq = 0;
    accesses_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}
