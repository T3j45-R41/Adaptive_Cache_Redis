---
tags: [dbms, buffer-pool, adaptive, cacheus, online-learning, project/adaptive-cache-redis]
status: implemented
---

# Adaptive Replacement

## Definition

Static replacement policies assume the workload is stationary. Real workloads
are not: popularity shifts. The literature's response is an **adaptive** policy
that learns from its own eviction history instead of committing to one rule.

**CacheUS** (Vietri et al., 2022) is the reference implementation. Its key idea
is a segmented cache:

| Segment | Purpose |
| --- | --- |
| **Window** | LRU admission — absorbs bursty new keys |
| **Main / Probationary** | LRU-ordered; admitted to the protected set after `s` hits |
| **Protected** | LRU-ordered; hot keys, with an eviction limit to prevent hoarding |
| **Training** | Sampled accesses with metadata for the ML model |

Plus a **ghost cache** of evicted keys' metadata (never their data) so the model
learns from keys it discarded, and an **adaptive sampling rate**: as the cache
fills, fewer accesses are sampled, to keep training cost bounded.

The **cost model** is trained online by logistic regression on features such as
reuse distance, frequency, object size, and time since last access. Its job is to
predict, for a candidate key, the probability it will be re-referenced soon
enough to repay its own eviction cost.

---

## In this project

> [!warning] Naming — read this before you present
> This is **not** CacheUS. It is CacheUS-*inspired*: it shares the idea of
> scoring the unchosen alternative in the shadow, and nothing else.
>
> | CacheUS feature | Present here |
> | --- | --- |
> | Sliding access window | ❌ |
> | Segmented cache (Window/Probationary/Protected) | ❌ |
> | Ghost cache | ❌ |
> | ML cost model (logistic regression) | ❌ |
> | Online gradient training | ❌ |
> | Adaptive sampling rate | ❌ |
> | Shadow evaluation of an alternative | ✅ |
> | Weight update from observed loss | ✅ |
>
> It is named `ADAPTIVE` throughout. `CACHEUS` is accepted only as a legacy
> command-line alias so older scripts keep running.
> **Do not claim CacheUS in a viva.** If asked directly, this table is the answer.

### The controller

`include/eviction/adaptive_policy.h`
```cpp
class AdaptivePolicy final : public EvictionPolicy {
private:
    LRUPolicy lru_;
    LFUPolicy lfu_;

    double learningRate_;
    double lossDecay_;
    // Neither expert may be driven to zero: a dead expert can never recover
    // and the controller would stop adapting to a later workload shift.
    static constexpr double kMinWeight_ = 0.05;

    double weightLRU_{0.5};
    double weightLFU_{0.5};
    ...
};
```

Both experts are maintained **in lockstep** over the same resident set, so only
one key is ever really evicted.

### The feedback loop

`src/eviction/adaptive_policy.cpp`
```cpp
void AdaptivePolicy::observeAccessLoss(const std::string& key) {
    // A shadow candidate that turns out to be wanted means the expert that
    // nominated it mis-ordered the cache. That is the only feedback signal
    // available without running the alternative to completion.
    if (pendingLRU_.active && pendingLRU_.key == key) {
        lossLRU_ += 1.0;
        pendingLRU_.active = false;
    }
    ...
}
```

`src/eviction/adaptive_policy.cpp`
```cpp
void AdaptivePolicy::registerEvictionDecision(const std::string&) {
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
```

That is **exponential-weights regret minimisation** over two experts. The
expert with lower shadow loss gains weight; the relative gap drives the update.

### Two bugs that made it not adapt

> [!warning] Both found and fixed
> **1. Losses never decayed.** `lossLRU_`/`lossLFU_` grew monotonically for the
> process lifetime, so `exp(-lr · regret)` shrank toward zero and the weights
> froze at whatever the first few thousand operations produced. Fixed by
> multiplying both losses by `lossDecay_` each round.
>
> **2. A weight floor.** Without a floor, one expert could be driven to zero
> weight and never recover, so a later workload shift would go unadapted.
> `kMinWeight_ = 0.05` guarantees both stay viable.

There is a third limitation that was **not** fixed, because it is inherent to
the design rather than a bug: with two discrete experts and a hard
`weightLRU_ >= weightLFU_` comparison, the effective policy is always exactly
"pure LRU" or "pure LFU", never a blend. Genuine interpolation would need a
randomised or softmax choice over candidates.

### Observability

`src/eviction/eviction_engine.cpp`
```cpp
std::string EvictionEngine::policyDetail() const {
    const auto* adaptive = dynamic_cast<const AdaptivePolicy*>(policy_.get());
    if (!adaptive) return "";

    oss << "lru_weight=" << adaptive->lruWeight()
        << " lfu_weight=" << adaptive->lfuWeight()
        << " lru_shadow_loss=" << adaptive->lruLoss()
        << " lfu_shadow_loss=" << adaptive->lfuLoss()
        << " adaptive_decisions=" << adaptive->decisions()
        << " preferred_expert=" << adaptive->preferredExpert();
```

Surfaced through `INFO` as `adaptive_detail`, and shown live in the Streamlit
panel's Dashboard tab.

---

## What would a real CacheUS need?

> [!tip] Prioritised extension path
> 1. **Sliding window** over recent accesses with reuse-distance tracking.
> 2. **Ghost cache** — metadata-only keys evicted recently, so a re-access can
>    be learned from instead of merely counted as a miss.
> 3. **Segmented cache** — window / probationary / protected, with a cap on how
>    much the protected segment may hoard.
> 4. **Cost model** — logistic regression over reuse distance, size, frequency,
>    and time-since-access; trained online on a sampled access stream.
> 5. **Adaptive sampling** — increase sampling as the cache fills.

Steps 1–2 are small and would immediately make the policy genuinely more
adaptive. Steps 3–5 are where the published method lives.

---

## Related

- [[DBMS/Buffer Replacement Policies]] — the experts it chooses between
- [[DBMS/Buffer Pool]] — what it manages
- [[DBMS/Query Processing]] — cost models in their full form
- [[Project/Benchmarking]] — the workload shift it is meant to react to
- [[Project/Gaps and Future Work]] — the extension plan