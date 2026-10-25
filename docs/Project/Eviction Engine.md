---
tags: [project/adaptive-cache-redis, eviction, architecture]
status: implemented
---

# Eviction Engine

## Role

The seam between the storage engine and the policy library. `RedisDatabase`
calls into it on every insert, access and removal, and asks it for a victim when
the cache is over capacity. It never touches a concrete policy type.

`include/eviction/eviction_engine.h`
```cpp
class EvictionEngine {
public:
    static bool isKnownPolicy(const std::string& name);
    static std::string canonicalPolicyName(const std::string& name);
    static std::vector<std::string> policyNames();

    bool setPolicy(const std::string& name);
    const std::string& policyName() const;
    const EvictionPolicy& policy() const;

    // 0 disables capacity enforcement entirely.
    void setMaxKeys(std::size_t keys);
    std::size_t maxKeys() const;
    bool capacityLimited() const;

    void onInsert(const std::string& key);
    void onAccess(const std::string& key);
    void onReplace(const std::string& key);
    void onRemove(const std::string& key);
    void onCapacityEnforced();
    std::string selectVictim() const;
    // Must be called *before* the victim is removed, while both experts still
    // agree on the resident set and can each nominate a shadow candidate.
    void registerEvictionDecisionFeedback(const std::string& victim);
    void resync(const std::vector<std::string>& keys);

    void recordHit();
    void recordMiss();
    void recordEviction(bool dirty);
    ...
};
```

Not internally synchronised — the owning database serialises access under
`db_mutex`.

---

## Policy resolution

`src/eviction/eviction_engine.cpp:44`
```cpp
std::string EvictionEngine::canonicalPolicyName(const std::string& name) {
    std::string key = upper(name);
    if (key == "CACHEUS") key = "ADAPTIVE"; // legacy alias
    const auto names = policyNames();
    if (std::find(names.begin(), names.end(), key) != names.end()) return key;
    return "";
}
```

> [!info] Why the alias survives
> `Benchmark/benchmark_client.py` and older documentation still pass
> `--policy CACHEUS`. Accepting it means those runs keep working, while
> `INFO` and every label report the truth: `ADAPTIVE`.

A rejected name leaves the active policy untouched — `setPolicy` returns
`false` and the caller surfaces `-ERR unknown policy ...`.

---

## Resynchronisation

A freshly constructed policy knows nothing. After a policy switch or a snapshot
load, the resident keys must be handed over **in insertion order**.

`src/RedisDatabase.cpp:90`–`src/RedisDatabase.cpp:107`
```cpp
void RedisDatabase::resyncPolicyLocked() {
    // Hand the keys over oldest-first. FIFO cannot recover insertion order
    // from an unordered set, so without this a policy switch or a snapshot
    // reload would leave it evicting in hash order.
    std::vector<std::pair<std::uint64_t, std::string>> ordered;
    ordered.reserve(store.size());
    for (const auto& entry : store) {
        const auto frame = frames.find(entry.first);
        ordered.emplace_back(frame == frames.end() ? 0 : frame->second.insertionTime,
                              entry.first);
    }
    std::sort(ordered.begin(), ordered.end());

    std::vector<std::string> keys;
    keys.reserve(ordered.size());
    for (auto& entry : ordered) keys.push_back(std::move(entry.second));

    engine.resync(keys);
}
```

> [!warning] A bug this prevents, with a test
> Without the sort, `FIFOPolicy::syncKeys` receives keys in `unordered_map`
> iteration order — effectively arbitrary — so after a policy switch FIFO
> evicted in hash order rather than insertion order.
>
> `testPolicySwitchKeepsResidentSet` sets `a`, `b`, `c`, switches to FIFO, adds
> `d`, and asserts that **`a`** is the victim. It also proves eviction does not
> stall: without a resync the new policy nominates nothing and the cache would
> silently exceed its limit.

---

## The eviction sequence

`src/RedisDatabase.cpp:110`–`src/RedisDatabase.cpp:129`

```cpp
while (store.size() > engine.maxKeys()) {
    const std::string victim = engine.selectVictim();
    if (victim.empty()) break;                 // policy has no candidate

    const bool dirty = frames.count(victim) ? frames[victim].dirty : false;

    engine.registerEvictionDecisionFeedback(victim);   // BEFORE removal
    removeKeyLocked(victim, RemovalReason::Evicted);
    engine.recordEviction(dirty);
    engine.onCapacityEnforced();
}
```

> [!important] Ordering is load-bearing
> `registerEvictionDecisionFeedback` must run **before** `removeKeyLocked`.
> After removal the experts no longer agree on the resident set, so the shadow
> candidate the adaptive controller needs would already be gone.

The loop is written generally rather than as a single `if`, so that lowering the
capacity limit at runtime drains the cache correctly:

```cpp
if (engine.setMaxKeys(limit)) enforceCapacityLocked();
```

---

## Counters

`include/eviction/eviction_metrics.h`
```cpp
struct EvictionMetrics {
    std::uint64_t totalAccesses{0};
    std::uint64_t totalHits{0};
    std::uint64_t totalMisses{0};
    std::uint64_t totalKeysInserted{0};

    std::uint64_t evictions{0};
    std::uint64_t writes{0};        // dirty frames written back on eviction
    std::uint64_t cleanEvictions{0};

    std::uint64_t lruSelections{0};
    std::uint64_t lfuSelections{0};
    std::uint64_t fifoSelections{0};
    std::uint64_t randomSelections{0};
    std::uint64_t adaptiveSelections{0};

    std::uint64_t expirations{0};
    std::uint64_t explicitDeletes{0};
    std::uint64_t policyChanges{0};
    std::uint64_t capacityEnforcements{0};
};
```

These were originally declared and never referenced — dead code. Wiring the
engine to them is what made eviction measurable, which is in turn what made the
benchmark report real numbers instead of `N/A`.

---

## Runtime configuration

| Command | Effect |
| --- | --- |
| `EVICTION <name>` / `CONFIG SET POLICY <name>` | Switch policy, resync resident set |
| `EVICTION` / `CONFIG GET POLICY` | Read the active policy |
| `MAXKEYS <n>` / `CONFIG SET MAXKEYS <n>` | Set capacity, evict down to it |
| `MAXKEYS` / `CONFIG GET MAXKEYS` | Read the limit |
| `CONFIG RESETSTAT` | Zero all counters |
| `INFO` | Full snapshot, including `adaptive_detail` |

---

## Related

- [[DBMS/Buffer Replacement Policies]] — the policies themselves
- [[DBMS/Adaptive Replacement]] — the adaptive controller
- [[DBMS/Buffer Pool]] — the buffer manager that calls this
- [[Project/Benchmarking]] — what the counters are compared with