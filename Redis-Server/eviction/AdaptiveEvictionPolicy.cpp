#include "../include/AdaptiveEvictionPolicy.h"
#include <algorithm>

AdaptiveEvictionPolicy::AdaptiveEvictionPolicy(size_t capacity, double learningRate, unsigned int seed)
    : capacity(capacity),
      learningRate(learningRate),
      initial_seed(seed),
      rng(seed),
      expertLRU(capacity),
      expertLFU(capacity),
      w1(0.5),
      w2(0.5) {}

void AdaptiveEvictionPolicy::onAccess(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    accesses_++;
    expertLRU.onAccess(key);
    expertLFU.onAccess(key);
}

void AdaptiveEvictionPolicy::onInsert(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // When a key is inserted, check if it was in any shadow history (miss with feedback)
    double r1 = 0.0;
    double r2 = 0.0;
    bool feedback = false;

    if (shadow1Set.count(key) > 0) {
        // Was recently evicted by LRU/Expert 1, or Expert 1's shadow history
        // If it was in shadow1, Expert 1 receives credit, Expert 2 gets regret
        r2 = 1.0;
        feedback = true;
        shadow1Set.erase(key);
    } else if (shadow2Set.count(key) > 0) {
        // Was in shadow2, Expert 2 receives credit, Expert 1 gets regret
        r1 = 1.0;
        feedback = true;
        shadow2Set.erase(key);
    }

    if (feedback) {
        cumulativeRegret1 += r1;
        cumulativeRegret2 += r2;

        // Hedge multiplicative weight update
        w1 = w1 * std::exp(-learningRate * r1);
        w2 = w2 * std::exp(-learningRate * r2);

        // Prevent numerical underflow
        if (w1 < 0.001) w1 = 0.001;
        if (w2 < 0.001) w2 = 0.001;

        // Normalize weights
        double sum = w1 + w2;
        w1 /= sum;
        w2 /= sum;
    }

    expertLRU.onInsert(key);
    expertLFU.onInsert(key);
}

void AdaptiveEvictionPolicy::onUpdate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    expertLRU.onUpdate(key);
    expertLFU.onUpdate(key);
}

void AdaptiveEvictionPolicy::onDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    expertLRU.onDelete(key);
    expertLFU.onDelete(key);
    shadow1Set.erase(key);
    shadow2Set.erase(key);
}

void AdaptiveEvictionPolicy::addToShadow(std::deque<std::string>& shadowQueue,
                                         std::unordered_set<std::string>& shadowSet,
                                         const std::string& key) {
    if (shadowSet.count(key)) return;
    if (shadowQueue.size() >= capacity) {
        std::string oldest = shadowQueue.front();
        shadowQueue.pop_front();
        shadowSet.erase(oldest);
    }
    shadowQueue.push_back(key);
    shadowSet.insert(key);
}

std::string AdaptiveEvictionPolicy::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Probabilistically or weight-proportionally select expert
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double r = dist(rng);

    std::string victim;
    if (r < w1) {
        // Choose Expert 1 (LRU)
        lastSelectedExpert = "LRU";
        expert1Selections++;
        victim = expertLRU.evict();
        if (victim.empty()) {
            victim = expertLFU.evict();
        } else {
            // Also notify LFU expert of deletion
            expertLFU.onDelete(victim);
        }
        if (!victim.empty()) {
            addToShadow(shadow1Queue, shadow1Set, victim);
        }
    } else {
        // Choose Expert 2 (LFU)
        lastSelectedExpert = "LFU";
        expert2Selections++;
        victim = expertLFU.evict();
        if (victim.empty()) {
            victim = expertLRU.evict();
        } else {
            // Also notify LRU expert of deletion
            expertLRU.onDelete(victim);
        }
        if (!victim.empty()) {
            addToShadow(shadow2Queue, shadow2Set, victim);
        }
    }

    if (!victim.empty()) {
        evictions_++;
    }
    return victim;
}

void AdaptiveEvictionPolicy::recordHit() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_++;
}

void AdaptiveEvictionPolicy::recordMiss() {
    std::lock_guard<std::mutex> lock(mutex_);
    misses_++;
}

void AdaptiveEvictionPolicy::onCacheMissFeedback(const std::string& missedKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    double r1 = 0.0;
    double r2 = 0.0;
    bool feedback = false;

    if (shadow1Set.count(missedKey) > 0) {
        r2 = 1.0;
        feedback = true;
        shadow1Set.erase(missedKey);
    } else if (shadow2Set.count(missedKey) > 0) {
        r1 = 1.0;
        feedback = true;
        shadow2Set.erase(missedKey);
    }

    if (feedback) {
        cumulativeRegret1 += r1;
        cumulativeRegret2 += r2;

        w1 = w1 * std::exp(-learningRate * r1);
        w2 = w2 * std::exp(-learningRate * r2);

        if (w1 < 0.001) w1 = 0.001;
        if (w2 < 0.001) w2 = 0.001;

        double sum = w1 + w2;
        w1 /= sum;
        w2 /= sum;
    }
}

double AdaptiveEvictionPolicy::getExpert1Weight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return w1;
}

double AdaptiveEvictionPolicy::getExpert2Weight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return w2;
}

std::string AdaptiveEvictionPolicy::getCurrentActiveExpert() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSelectedExpert;
}

EvictionStats AdaptiveEvictionPolicy::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EvictionStats stats;
    stats.policyName = "ADAPTIVE";
    stats.totalAccesses = accesses_;
    stats.totalHits = hits_;
    stats.totalMisses = misses_;
    stats.totalEvictions = evictions_;
    stats.currentSize = expertLRU.getStats().currentSize;
    stats.capacity = capacity;
    stats.expert1Weight = w1;
    stats.expert2Weight = w2;
    stats.cumulativeRegret1 = cumulativeRegret1;
    stats.cumulativeRegret2 = cumulativeRegret2;
    stats.expert1Selections = expert1Selections;
    stats.expert2Selections = expert2Selections;
    stats.currentActiveExpert = lastSelectedExpert;
    stats.learningRate = learningRate;
    return stats;
}

void AdaptiveEvictionPolicy::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    expertLRU.reset();
    expertLFU.reset();
    w1 = 0.5;
    w2 = 0.5;
    cumulativeRegret1 = 0.0;
    cumulativeRegret2 = 0.0;
    expert1Selections = 0;
    expert2Selections = 0;
    lastSelectedExpert = "LRU";
    shadow1Queue.clear();
    shadow1Set.clear();
    shadow2Queue.clear();
    shadow2Set.clear();
    rng.seed(initial_seed);
    accesses_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}
