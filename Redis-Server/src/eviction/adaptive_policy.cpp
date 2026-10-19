#include "adaptive_policy.h"
#include <algorithm>
#include <cmath>

AdaptivePolicy::AdaptivePolicy(double learningRate, double lossDecay)
    : learningRate_(learningRate), lossDecay_(lossDecay) {}

void AdaptivePolicy::onInsert(const std::string& key) {
    lru_.onInsert(key);
    lfu_.onInsert(key);
}

void AdaptivePolicy::observeAccessLoss(const std::string& key) {
    // A shadow candidate that turns out to be wanted means the expert that
    // nominated it mis-ordered the cache. That is the only feedback signal
    // available without running the alternative to completion.
    if (pendingLRU_.active && pendingLRU_.key == key) {
        lossLRU_ += 1.0;
        pendingLRU_.active = false;
    }
    if (pendingLFU_.active && pendingLFU_.key == key) {
        lossLFU_ += 1.0;
        pendingLFU_.active = false;
    }
}

void AdaptivePolicy::onAccess(const std::string& key) {
    observeAccessLoss(key);
    lru_.onAccess(key);
    lfu_.onAccess(key);
}

void AdaptivePolicy::onRemove(const std::string& key) {
    lru_.onRemove(key);
    lfu_.onRemove(key);
    if (pendingLRU_.active && pendingLRU_.key == key) pendingLRU_.active = false;
    if (pendingLFU_.active && pendingLFU_.key == key) pendingLFU_.active = false;
}

void AdaptivePolicy::onReplace(const std::string& key) {
    // Overwriting a value is neither a read nor a new arrival, so neither the
    // recency nor the frequency of the key may move.
    lru_.onReplace(key);
    lfu_.onReplace(key);
}

void AdaptivePolicy::normalizeWeights() {
    weightLRU_ = std::max(weightLRU_, kMinWeight_);
    weightLFU_ = std::max(weightLFU_, kMinWeight_);
    const double total = weightLRU_ + weightLFU_;
    if (total <= 0.0) {
        weightLRU_ = weightLFU_ = 0.5;
        return;
    }
    weightLRU_ /= total;
    weightLFU_ /= total;
}

void AdaptivePolicy::registerEvictionDecision(const std::string& /*selectedVictim*/) {
    ++decisions_;

    // Snapshot both experts' candidates before the owner removes anything, so
    // the unselected candidate survives as a shadow for the next round.
    pendingLRU_.key = lru_.selectVictim();
    pendingLRU_.active = !pendingLRU_.key.empty();
    pendingLFU_.key = lfu_.selectVictim();
    pendingLFU_.active = !pendingLFU_.key.empty();

    // Forget older evidence. Without this the cumulative losses only ever
    // grow, the exponential update shrinks to a no-op, and the weights freeze
    // at whatever the first few thousand operations produced.
    lossLRU_ *= lossDecay_;
    lossLFU_ *= lossDecay_;

    const double minLoss = std::min(lossLRU_, lossLFU_);
    const double regretLRU = lossLRU_ - minLoss;
    const double regretLFU = lossLFU_ - minLoss;

    weightLRU_ *= std::exp(-learningRate_ * regretLRU);
    weightLFU_ *= std::exp(-learningRate_ * regretLFU);
    normalizeWeights();
}

std::string AdaptivePolicy::selectVictim() const {
    const std::string lruVictim = lru_.selectVictim();
    const std::string lfuVictim = lfu_.selectVictim();

    if (lruVictim.empty()) return lfuVictim;
    if (lfuVictim.empty()) return lruVictim;

    return (weightLRU_ >= weightLFU_) ? lruVictim : lfuVictim;
}

void AdaptivePolicy::syncKeys(const std::vector<std::string>& keys) {
    lru_.syncKeys(keys);
    lfu_.syncKeys(keys);
}