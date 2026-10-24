#include "eviction_engine.h"

#include "adaptive_policy.h"
#include "fifo_policy.h"
#include "lfu_policy.h"
#include "lru_policy.h"
#include "random_policy.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string upper(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::unique_ptr<EvictionPolicy> makePolicy(const std::string& canonical) {
    if (canonical == "LRU")     return std::make_unique<LRUPolicy>();
    if (canonical == "LFU")     return std::make_unique<LFUPolicy>();
    if (canonical == "FIFO")    return std::make_unique<FIFOPolicy>();
    if (canonical == "RANDOM")  return std::make_unique<RandomPolicy>();
    if (canonical == "ADAPTIVE")return std::make_unique<AdaptivePolicy>();
    return nullptr;
}

} // namespace

EvictionEngine::EvictionEngine() {
    policy_ = makePolicy("ADAPTIVE");
    policyName_ = "ADAPTIVE";
}

EvictionEngine::~EvictionEngine() = default;

std::vector<std::string> EvictionEngine::policyNames() {
    return {"LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"};
}

std::string EvictionEngine::canonicalPolicyName(const std::string& name) {
    std::string key = upper(name);
    if (key == "CACHEUS") key = "ADAPTIVE"; // legacy alias
    const auto names = policyNames();
    if (std::find(names.begin(), names.end(), key) != names.end()) return key;
    return "";
}

bool EvictionEngine::isKnownPolicy(const std::string& name) {
    return !canonicalPolicyName(name).empty();
}

bool EvictionEngine::setPolicy(const std::string& name) {
    const std::string canonical = canonicalPolicyName(name);
    if (canonical.empty()) return false;

    auto next = makePolicy(canonical);
    if (!next) return false;

    // The new policy starts empty. The owner knows which keys are resident and
    // must call resync() afterwards, otherwise a mid-run policy switch would
    // nominate nothing and eviction would stall until the keys are rewritten.
    policy_ = std::move(next);
    policyName_ = canonical;
    ++metrics_.policyChanges;
    return true;
}

void EvictionEngine::setMaxKeys(std::size_t keys) { maxKeys_ = keys; }

void EvictionEngine::onInsert(const std::string& key) {
    policy_->onInsert(key);
    ++metrics_.totalKeysInserted;
}

void EvictionEngine::onAccess(const std::string& key) {
    policy_->onAccess(key);
    ++metrics_.totalAccesses;
}

void EvictionEngine::onReplace(const std::string& /*key*/) {
    ++metrics_.totalAccesses;
}

void EvictionEngine::onRemove(const std::string& key) { policy_->onRemove(key); }

void EvictionEngine::onCapacityEnforced() { ++metrics_.capacityEnforcements; }

std::string EvictionEngine::selectVictim() const { return policy_->selectVictim(); }

void EvictionEngine::registerEvictionDecisionFeedback(const std::string& victim) {
    policy_->registerEvictionDecision(victim);
}

void EvictionEngine::resync(const std::vector<std::string>& keys) { policy_->syncKeys(keys); }

void EvictionEngine::recordHit() {
    ++metrics_.totalHits;
    ++metrics_.totalAccesses;
}

void EvictionEngine::recordMiss() {
    ++metrics_.totalMisses;
    ++metrics_.totalAccesses;
}

void EvictionEngine::recordEviction(bool dirty) {
    ++metrics_.evictions;
    if (dirty) ++metrics_.writes;
    else ++metrics_.cleanEvictions;
    bumpPolicySelection();
}

void EvictionEngine::recordExpiration() { ++metrics_.expirations; }

void EvictionEngine::recordExplicitDelete() { ++metrics_.explicitDeletes; }

void EvictionEngine::resetStats() { metrics_.reset(); }

void EvictionEngine::bumpPolicySelection() {
    if (policyName_ == "LRU")        ++metrics_.lruSelections;
    else if (policyName_ == "LFU")   ++metrics_.lfuSelections;
    else if (policyName_ == "FIFO")  ++metrics_.fifoSelections;
    else if (policyName_ == "RANDOM")++metrics_.randomSelections;
    else if (policyName_ == "ADAPTIVE") ++metrics_.adaptiveSelections;
}

std::string EvictionEngine::policyDetail() const {
    const auto* adaptive = dynamic_cast<const AdaptivePolicy*>(policy_.get());
    if (!adaptive) return "";

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << "lru_weight=" << adaptive->lruWeight()
        << " lfu_weight=" << adaptive->lfuWeight()
        << " lru_shadow_loss=" << adaptive->lruLoss()
        << " lfu_shadow_loss=" << adaptive->lfuLoss()
        << " adaptive_decisions=" << adaptive->decisions()
        << " preferred_expert=" << adaptive->preferredExpert();
    return oss.str();
}