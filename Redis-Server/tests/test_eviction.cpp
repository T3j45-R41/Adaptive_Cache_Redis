// Unit tests for the eviction policy library and the engine that wraps it.
//
// These link against src/eviction/*.cpp only, with no server, no sockets and
// no database, so they exercise the policies as pure algorithms.
#include "eviction/adaptive_policy.h"
#include "eviction/eviction_engine.h"
#include "eviction/fifo_policy.h"
#include "eviction/lfu_policy.h"
#include "eviction/lru_policy.h"
#include "eviction/random_policy.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_currentTest;

void check(bool condition, const std::string& what, int line) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::cout << "  FAIL [" << g_currentTest << ":" << line << "] " << what << "\n";
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected)                                                  \
    do {                                                                            \
        const auto a_ = (actual);                                                    \
        const auto e_ = (expected);                                                  \
        ++g_checks;                                                                  \
        if (!(a_ == e_)) {                                                           \
            ++g_failures;                                                            \
            std::cout << "  FAIL [" << g_currentTest << ":" << __LINE__ << "] "       \
                      << #actual << "\n        got:      " << a_                      \
                      << "\n        expected: " << e_ << "\n";                        \
        }                                                                            \
    } while (false)

struct TestScope {
    explicit TestScope(const std::string& name) {
        g_currentTest = name;
        std::cout << "- " << name << "\n";
    }
};

// ---------------------------------------------------------------------------

void testLru() {
    TestScope scope("LRU orders by recency");
    LRUPolicy policy;

    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    CHECK_EQ(policy.selectVictim(), std::string("A"));

    policy.onAccess("A");
    CHECK_EQ(policy.selectVictim(), std::string("B"));

    policy.onAccess("B");
    CHECK_EQ(policy.selectVictim(), std::string("C"));

    policy.onRemove("C");
    CHECK_EQ(policy.selectVictim(), std::string("A"));

    // Re-inserting a live key must not duplicate it in the order list.
    policy.onInsert("A");
    policy.onAccess("A");
    policy.onRemove("A");
    CHECK_EQ(policy.selectVictim(), std::string("B"));

    policy.onRemove("B");
    CHECK(policy.selectVictim().empty());
}

void testLfuKeepsFrequencyAcrossReinsert() {
    TestScope scope("LFU does not reset frequency on re-insert");
    LFUPolicy policy;

    policy.onInsert("hot");
    policy.onInsert("cold");
    for (int i = 0; i < 5; ++i) policy.onAccess("hot");

    CHECK_EQ(policy.selectVictim(), std::string("cold"));

    // The original code overwrote the counter here, which would have promoted
    // "cold" above a key that had been read five times.
    policy.onInsert("hot");
    CHECK_EQ(policy.selectVictim(), std::string("cold"));

    // One more read of "cold" is not enough: LFU deliberately resists a single
    // recent access, so the colder key is still the victim.
    policy.onAccess("cold");
    CHECK_EQ(policy.selectVictim(), std::string("cold"));

    // Only once "cold" overtakes "hot" on frequency does the victim flip,
    // which also proves the re-insert never zeroed "hot"'s counter.
    for (int i = 0; i < 5; ++i) policy.onAccess("cold");
    CHECK_EQ(policy.selectVictim(), std::string("hot"));
}

void testLfuTieBreaksByRecency() {
    TestScope scope("LFU breaks frequency ties by oldest access");
    LFUPolicy policy;

    policy.onInsert("a");
    policy.onInsert("b");
    policy.onAccess("a"); // a is now more recent than b at equal frequency
    CHECK_EQ(policy.selectVictim(), std::string("b"));
}

void testLfuUnknownAccessIsInserted() {
    TestScope scope("LFU treats access to an unknown key as an insert");
    LFUPolicy policy;
    policy.onAccess("x");
    policy.onInsert("y");
    // Both at frequency 1, so the older entry loses.
    CHECK_EQ(policy.selectVictim(), std::string("x"));
}

void testFifoIgnoresAccess() {
    TestScope scope("FIFO ignores access order");
    FIFOPolicy policy;

    policy.onInsert("A");
    policy.onInsert("B");
    policy.onAccess("B");
    policy.onAccess("B");
    CHECK_EQ(policy.selectVictim(), std::string("A"));

    policy.onAccess("A");
    CHECK_EQ(policy.selectVictim(), std::string("A"));
}

void testFifoStaysBounded() {
    TestScope scope("FIFO reclaims queue slots after removals");
    FIFOPolicy policy;

    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");

    // Removing the head must let the queue advance, not leave a tombstone that
    // blocks selectVictim.
    policy.onRemove("A");
    CHECK_EQ(policy.selectVictim(), std::string("B"));
    policy.onRemove("B");
    CHECK_EQ(policy.selectVictim(), std::string("C"));
    policy.onRemove("C");
    CHECK(policy.selectVictim().empty());
}

void testRandomOnlyReturnsMembers() {
    TestScope scope("Random nominates only resident keys");
    RandomPolicy policy;
    CHECK(policy.selectVictim().empty());

    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    policy.onInsert("A"); // duplicate insert must not add a second copy

    std::set<std::string> drawn;
    for (int i = 0; i < 200; ++i) {
        const std::string victim = policy.selectVictim();
        CHECK(victim == "A" || victim == "B" || victim == "C");
        drawn.insert(victim);
    }
    CHECK(drawn.size() > 1); // over 200 draws a single-key outcome is implausible

    policy.onRemove("B");
    for (int i = 0; i < 200; ++i) CHECK(policy.selectVictim() != "B");
}

void testAdaptiveAdaptsAndKeepsFloor() {
    TestScope scope("Adaptive shifts weight away from the losing expert");
    AdaptivePolicy policy;

    for (int i = 0; i < 5; ++i) policy.onInsert("k" + std::to_string(i));
    CHECK(policy.preferredExpert() == "LRU" || policy.preferredExpert() == "LFU");

    // Every decision hands the adaptive controller a shadow signal. The weights
    // must stay within bounds and must not collapse to a single dead expert.
    // A fresh key is inserted each round so the resident set stays populated,
    // the way a live cache under load would be.
    std::string victim;
    for (int i = 0; i < 500; ++i) {
        policy.onInsert("k" + std::to_string(1000 + i));
        victim = policy.selectVictim();
        CHECK(!victim.empty());
        policy.registerEvictionDecision(victim);
        policy.onRemove(victim);
    }

    CHECK_EQ(policy.decisions(), std::uint64_t(500));
    CHECK(policy.lruWeight() > 0.0);
    CHECK(policy.lfuWeight() > 0.0);
    // The floor guarantees an expert is never driven out entirely.
    CHECK(policy.lruWeight() >= 0.05 - 1e-9);
    CHECK(policy.lfuWeight() >= 0.05 - 1e-9);
    CHECK(std::abs(policy.lruWeight() + policy.lfuWeight() - 1.0) < 1e-9);
}

void testAdaptiveShadowLossDecays() {
    TestScope scope("Adaptive loss decays so the weights keep moving");
    AdaptivePolicy policy;

    policy.onInsert("a");
    policy.onInsert("b");

    // With two keys both experts nominate the same candidate, so the snapshot
    // is deterministic: "a".
    const std::string victim = policy.selectVictim();
    CHECK_EQ(victim, std::string("a"));
    policy.registerEvictionDecision(victim);

    // Re-reading a candidate the controller was about to evict is the loss signal.
    policy.onAccess(victim);
    const double peak = policy.lruLoss();
    CHECK(peak > 0.0);

    // Decay must pull the accumulated loss back down over time.
    for (int i = 0; i < 50; ++i) policy.registerEvictionDecision(policy.selectVictim());
    CHECK(policy.lruLoss() < peak);
}

void testPolicyReplaceKeepsOrdering() {
    TestScope scope("Rewriting a value moves neither recency nor frequency");
    LRUPolicy lru;
    LFUPolicy lfu;

    for (EvictionPolicy* policy : {static_cast<EvictionPolicy*>(&lru),
                                   static_cast<EvictionPolicy*>(&lfu)}) {
        policy->onInsert("a");
        policy->onInsert("b");
        policy->onAccess("a");
        const std::string before = policy->selectVictim();
        policy->onReplace("a");
        CHECK_EQ(policy->selectVictim(), before);
    }
}

template <typename Policy>
void testSyncKeys(Policy& policy, const std::string& label) {
    TestScope scope("syncKeys reconciles the resident set on " + label);

    policy.onInsert("keep");
    policy.onInsert("drop");
    policy.onAccess("keep");

    policy.syncKeys({"keep", "added"});
    // A key that left the resident set must never be nominated again, and a
    // newly added one must be visible.
    for (int i = 0; i < 20; ++i) {
        const std::string victim = policy.selectVictim();
        CHECK(victim == "keep" || victim == "added");
    }

    // Everything gone means no candidate, not a stale one.
    policy.syncKeys({});
    CHECK(policy.selectVictim().empty());
}

void testSyncKeys() {
    LRUPolicy lru; testSyncKeys(lru, "LRU");
    LFUPolicy lfu; testSyncKeys(lfu, "LFU");
    FIFOPolicy fifo; testSyncKeys(fifo, "FIFO");
    RandomPolicy random; testSyncKeys(random, "RANDOM");
    AdaptivePolicy adaptive; testSyncKeys(adaptive, "ADAPTIVE");
}

void testEnginePolicyFactory() {
    TestScope scope("Engine resolves policy names case-insensitively");

    CHECK(EvictionEngine::isKnownPolicy("lru"));
    CHECK(EvictionEngine::isKnownPolicy("LFU"));
    CHECK(EvictionEngine::isKnownPolicy("Adaptive"));
    // Legacy alias kept for the benchmark client and older docs.
    CHECK(EvictionEngine::isKnownPolicy("CACHEUS"));
    CHECK_EQ(EvictionEngine::canonicalPolicyName("cacheus"), std::string("ADAPTIVE"));
    CHECK(!EvictionEngine::isKnownPolicy("CLOCK"));
    CHECK(!EvictionEngine::isKnownPolicy(""));

    EvictionEngine engine;
    CHECK(engine.setPolicy("lru"));
    CHECK_EQ(engine.policyName(), std::string("LRU"));
    CHECK(!engine.setPolicy("nonsense"));
    CHECK_EQ(engine.policyName(), std::string("LRU")); // unchanged on failure
}

void testEngineCapacityAndMetrics() {
    TestScope scope("Engine counts accesses and evictions");

    EvictionEngine engine;
    CHECK(engine.setPolicy("LRU"));
    engine.setMaxKeys(2);
    CHECK(engine.capacityLimited());

    engine.recordHit();
    engine.recordHit();
    engine.recordMiss();
    CHECK_EQ(engine.metrics().totalHits, std::uint64_t(2));
    CHECK_EQ(engine.metrics().totalMisses, std::uint64_t(1));
    CHECK_EQ(engine.metrics().totalAccesses, std::uint64_t(3));
    CHECK_EQ(engine.metrics().hitRatePercent(), std::uint64_t(66)); // integer division

    engine.recordEviction(/*dirty=*/true);
    engine.recordEviction(/*dirty=*/false);
    CHECK_EQ(engine.metrics().evictions, std::uint64_t(2));
    CHECK_EQ(engine.metrics().writes, std::uint64_t(1));
    CHECK_EQ(engine.metrics().cleanEvictions, std::uint64_t(1));
    CHECK_EQ(engine.metrics().lruSelections, std::uint64_t(2));

    engine.resetStats();
    CHECK_EQ(engine.metrics().evictions, std::uint64_t(0));
    CHECK_EQ(engine.metrics().totalAccesses, std::uint64_t(0));
}

void testEngineSelectionCountersFollowPolicy() {
    TestScope scope("Engine attributes evictions to the active policy");
    EvictionEngine engine;

    engine.setPolicy("FIFO");
    engine.recordEviction(false);
    CHECK_EQ(engine.metrics().fifoSelections, std::uint64_t(1));
    CHECK_EQ(engine.metrics().lruSelections, std::uint64_t(0));

    engine.setPolicy("LFU");
    engine.recordEviction(true);
    CHECK_EQ(engine.metrics().lfuSelections, std::uint64_t(1));
    CHECK_EQ(engine.metrics().writes, std::uint64_t(1));
}

void testEngineDetailOnlyForAdaptive() {
    TestScope scope("Engine exposes controller state only when it exists");
    EvictionEngine engine;

    engine.setPolicy("LRU");
    CHECK(engine.policyDetail().empty());

    engine.setPolicy("ADAPTIVE");
    const std::string detail = engine.policyDetail();
    CHECK(!detail.empty());
    CHECK(detail.find("lru_weight=") != std::string::npos);
    CHECK(detail.find("preferred_expert=") != std::string::npos);
}

void testUnlimitedCapacity() {
    TestScope scope("Zero capacity disables enforcement");
    EvictionEngine engine;
    engine.setMaxKeys(0);
    CHECK(!engine.capacityLimited());
    CHECK(engine.selectVictim().empty());
}

} // namespace

int main() {
    std::cout << "Eviction policy tests\n";

    testLru();
    testLfuKeepsFrequencyAcrossReinsert();
    testLfuTieBreaksByRecency();
    testLfuUnknownAccessIsInserted();
    testFifoIgnoresAccess();
    testFifoStaysBounded();
    testRandomOnlyReturnsMembers();
    testAdaptiveAdaptsAndKeepsFloor();
    testAdaptiveShadowLossDecays();
    testPolicyReplaceKeepsOrdering();
    testSyncKeys();
    testEnginePolicyFactory();
    testEngineCapacityAndMetrics();
    testEngineSelectionCountersFollowPolicy();
    testEngineDetailOnlyForAdaptive();
    testUnlimitedCapacity();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures == 0) std::cout << "All eviction tests passed.\n";
    return g_failures == 0 ? 0 : 1;
}