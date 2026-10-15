#include "adaptive_policy.h"
#include <algorithm>

AdaptivePolicy::AdaptivePolicy(double learningRate): learningRate_(learningRate) {}

void AdaptivePolicy::onInsert(const std::string& key){
    lru_.onInsert(key);
    lfu_.onInsert(key);
}

void AdaptivePolicy::observeAccessLoss(const std::string& key){
    // Shadow interpretation:
    // If an expert would have evicted a key that is accessed soon afterwards,
    // that candidate receives a loss signal. This is a Phase-2 approximation
    // of shadow-policy feedback, not a claim of reproducing CACHEUS exactly.
    if(pendingLRU_.active && pendingLRU_.key==key){
        lossLRU_+=1.0;
        pendingLRU_.active=false;
    }
    if(pendingLFU_.active && pendingLFU_.key==key){
        lossLFU_ += 1.0;
        pendingLFU_.active = false;
    }
}

void AdaptivePolicy::onAccess(const std::string& key){
    observeAccessLoss(key);
    lru_.onAccess(key);
    lfu_.onAccess(key);
}

void AdaptivePolicy::onRemove(const std::string& key){
    lru_.onRemove(key);
    lfu_.onRemove(key);
    if(pendingLRU_.active && pendingLRU_.key==key) pendingLRU_.active = false;
    if(pendingLFU_.active && pendingLFU_.key==key) pendingLFU_.active = false;
}

void AdaptivePolicy::normalizeWeights(){
    const double total=weightLRU_+weightLFU_;
    if (total<=0.0){
        weightLRU_=weightLFU_=0.5;
        return;
    }
    weightLRU_/=total;
    weightLFU_/=total;
}

void AdaptivePolicy::registerEvictionDecision(const std::string& selectedVictim){
    ++decisions_;
    // Record each expert's proposed victim before the actual removal.
    // The non-selected expert is evaluated in the shadow.
    pendingLRU_.key = lru_.selectVictim();
    pendingLRU_.active = !pendingLRU_.key.empty();
    pendingLFU_.key = lfu_.selectVictim();
    pendingLFU_.active = !pendingLFU_.key.empty();
    // Convert the accumulated shadow loss into a regret-like update.
    // Lower loss => larger weight.
    const double minLoss = std::min(lossLRU_, lossLFU_);
    const double regretLRU = std::max(0.0, lossLRU_ - minLoss);
    const double regretLFU = std::max(0.0, lossLFU_ - minLoss);

    weightLRU_ *= std::exp(-learningRate_ * regretLRU);
    weightLFU_ *= std::exp(-learningRate_ * regretLFU);
    normalizeWeights();

    // The selected victim is used only as the actual cache decision.
    // The caller is responsible for removing it from the cache and calling
    // onRemove(selectedVictim).
    (void)selectedVictim;
}

std::string AdaptivePolicy::selectVictim() const{
    const std::string lruVictim = lru_.selectVictim();
    const std::string lfuVictim = lfu_.selectVictim();

    if (lruVictim.empty()) return lfuVictim;
    if (lfuVictim.empty()) return lruVictim;

    return (weightLRU_ >= weightLFU_)?lruVictim:lfuVictim;
}

void AdaptivePolicy::syncKeys(const std::vector<std::string>& keys){
    lru_.syncKeys(keys);
    lfu_.syncKeys(keys);
}
