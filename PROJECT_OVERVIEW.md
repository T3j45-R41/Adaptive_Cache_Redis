# Adaptive_Cache_Redis — Project Deep Dive

> A from-scratch, Redis-protocol-compatible cache server in C++17, built to run an
> experiment: **when the "hot" data in a cache shifts abruptly, does an adaptive
> eviction policy actually beat static ones?**
>
> This document explains what the project is, how it is put together, and how
> every piece works. It is a companion to `README.md` (overview + quick start),
> `CONTROL_PANEL_GUIDE.md` (the Streamlit UI), and `docs/` (a 32-note Obsidian
> concept vault).

---

## Table of Contents

1. [The Idea](#1-the-idea)
2. [Repository Layout](#2-repository-layout)
3. [The Central Analogy: This Is a Buffer Pool](#3-the-central-analogy-this-is-a-buffer-pool)
4. [The Eviction Policy Library](#4-the-eviction-policy-library)
   - 4.1 [The `EvictionPolicy` interface](#41-the-evictionpolicy-interface)
   - 4.2 [`CacheEntry` — the frame descriptor](#42-cacheentry--the-frame-descriptor)
   - 4.3 [`EvictionMetrics` — the counters](#43-evictionmetrics--the-counters)
   - 4.4 [The `EvictionEngine` — factory + metrics hub](#44-the-evictionengine--factory--metrics-hub)
   - 4.5 [LRU](#45-lru)
   - 4.6 [LFU](#46-lfu)
   - 4.7 [FIFO](#47-fifo)
   - 4.8 [RANDOM](#48-random)
   - 4.9 [ADAPTIVE — the research contribution](#49-adaptive--the-research-contribution)
5. [The C++ Server](#5-the-c-server)
   - 5.1 [`RedisDatabase` — storage + buffer manager](#51-redisdatabase--storage--buffer-manager)
   - 5.2 [`RedisServer` — transport and threading](#52-redisserver--transport-and-threading)
   - 5.3 [`RedisCommandHandler` — the RESP protocol](#53-rediscommandhandler--the-resp-protocol)
   - 5.4 [Persistence](#54-persistence)
6. [The Python Layer](#6-the-python-layer)
   - 6.1 [`redis_client.py`](#61-redis_clientpy)
   - 6.2 [`ui_app.py`](#62-ui_apppy)
7. [The Benchmark Harness](#7-the-benchmark-harness)
   - 7.1 [Workload generation](#71-workload-generation)
   - 7.2 [`benchmark_client.py`](#72-benchmark_clientpy)
   - 7.3 [`analyze_results.py`](#73-analyze_resultspy)
8. [The Documentation Vault](#8-the-documentation-vault)
9. [Build, Run, Test](#9-build-run-test)
10. [Design Decisions and Their Trade-offs](#10-design-decisions-and-their-trade-offs)
11. [Bugs Found and Fixed](#11-bugs-found-and-fixed)
12. [Quick Reference: Constants and Hyperparameters](#12-quick-reference-constants-and-hyperparameters)
13. [Known Gaps and Future Work](#13-known-gaps-and-future-work)
14. [How to Read the Results Honestly](#14-how-to-read-the-results-honestly)

---

## 1. The Idea

`Adaptive_Cache_Redis` is a **5th-semester Project-Based Learning (PBL) submission**
that turns a textbook DBMS problem into a running, measurable system.

The textbook problem: a database keeps a fixed-size **buffer pool** in memory.
When a page is needed and no frame is free, some page must be evicted. The
**replacement policy** decides which. Classic choices are LRU, LFU, FIFO, and
Random. Textbook rankings assume a stationary workload. Real workloads are not
stationary — an earnings announcement makes a previously cold ticker hot
overnight, and yesterday's hot set is now garbage.

So the research question is:

> When the hot set shifts suddenly, does an **adaptive** policy — one that learns
> *at runtime* which rule is currently right — beat any static rule?

To answer it you need three things, and the project builds all three:

| Need | Deliverable |
|---|---|
| A working cache with a *pluggable* eviction policy | C++17 server speaking RESP over TCP |
| A policy that can adapt | `ADAPTIVE` — two-expert exponential-weights controller |
| A workload where the shift is real, and honest measurement | Stock-quote generator + benchmark + analyser |

Everything else (Streamlit control panel, Obsidian vault) exists to make the
experiment inspectable and to demonstrate the OS/DBMS concepts it touches.

### What it is *not*

- **Not a fork of Redis.** Despite the directory name `Redis-Server/`, there is
  no upstream Redis source, no `.gitmodules`, no vendored code. It is an original
  C++17 implementation that happens to speak the Redis wire protocol, so that
  `redis-cli`, `redis-py`, and any RESP client can talk to it.
- **Not CacheUS.** `ADAPTIVE` is CacheUS-*inspired*: it shares CacheUS's central
  idea of evaluating an alternative in the shadow. It has **no** sliding access
  window, **no** segmented cache, **no** ghost cache, **no** ML cost model,
  **no** online gradient training. The project reports it as `ADAPTIVE`, never as
  a reproduction. (`CACHEUS` is accepted only as a legacy alias — see
  [§10.6](#106-legacy-name-compatibility).)

---

## 2. Repository Layout

```
Adaptive_Cache_Redis/
├── README.md                      # overview, quick start, layout
├── PROJECT_OVERVIEW.md            # ← this file
├── CONTROL_PANEL_GUIDE.md         # Streamlit app manual
├── requirements-ui.txt            # streamlit, yfinance, pandas
├── redis_client.py                # reusable RESP2 client (260 lines)
├── ui_app.py                      # Streamlit control panel (744 lines)
│
├── Redis-Server/                  # ── the C++17 server ──
│   ├── Makefile
│   ├── my_redis_server            # compiled binary
│   ├── dump.my_rdb                # snapshot (gitignore'd, but tracked)
│   ├── Concepts,UseCases&Tests.md
│   ├── README_OS_CONCEPTS.md
│   ├── test_all.sh
│   ├── include/
│   │   ├── RedisDatabase.h        # storage engine + buffer manager
│   │   ├── RedisServer.h          # sockets, threads, signals
│   │   ├── RedisCommandHandler.h  # RESP parser/dispatcher
│   │   └── eviction/              # 9 headers — the policy library
│   ├── src/
│   │   ├── main.cpp
│   │   ├── RedisDatabase.cpp      # 1015 lines
│   │   ├── RedisServer.cpp
│   │   ├── RedisCommandHandler.cpp # 618 lines
│   │   └── eviction/              # 6 sources — the policy library
│   ├── tests/
│   │   ├── test_eviction.cpp      # 388 lines, ~1069 checks
│   │   └── test_integration.cpp   # 813 lines, ~180 checks
│   └── build/                     # .o / .d / test binaries
│
├── eviction_policies/             # ⚠ STUB — see below
│   ├── CMakeLists.txt
│   └── README.md
│
├── Benchmark/                     # Python measurement harness
│   ├── generate_stock_workload.py
│   ├── benchmark_client.py
│   ├── analyze_results.py
│   ├── run_benchmark.bat
│   ├── run_experiments.bat
│   ├── run_analysis.bat
│   └── requirements.txt
│
├── tools/
│   └── validate_vault.py          # docs + source-reference validator (186 lines)
│
└── docs/                          # 32-note Obsidian concept vault
    ├── Home.md, README.md
    ├── OS/          (11 notes)
    ├── DBMS/        (11 notes)
    └── Project/     (8 notes)
```

### ⚠ `eviction_policies/` is a stub, not the implementation

This trips people up. `eviction_policies/` contains **only** a `CMakeLists.txt`
and a `README.md` — **no policy source code lives there**. Git history shows the
duplicate headers were deliberately deleted; `eviction_policies/README.md:15-19`
explains why:

> "This directory used to hold a second, byte-identical copy of every policy
> file. Two copies of the same code drift — and these did: only the copy under
> `src/` was ever compiled by the Makefile."

The `CMakeLists.txt` still globs `${SERVER_DIR}/src/eviction/*.cpp` (`:22`) and
builds the same sources as a static library, so CMake builds and Make builds
compile identical code.

**The real policy code is:**

```
Redis-Server/include/eviction/   (9 headers)
Redis-Server/src/eviction/       (6 sources)
Redis-Server/tests/test_eviction.cpp
```

---

## 3. The Central Analogy: This Is a Buffer Pool

The single most important idea in the project is stated in
`docs/Home.md:63-72` and repeated throughout `docs/DBMS/Buffer Pool.md`:

> The in-memory `unordered_map` store **is** the buffer pool.
> Eviction **is** page replacement.
> `write_backs` **is** the I/O metric.

The mapping:

| DBMS concept | This project |
|---|---|
| Page | `Value` (a string, list, or hash) |
| Page number | `std::string key` |
| Frame / frame descriptor | `CacheEntry` |
| Frame pool | `std::unordered_map<std::string, Value> store` |
| Dirty bit | `CacheEntry::dirty` |
| Frame size | `CacheEntry::byteSize` (computed, reported, **not** enforced) |
| Replacement policy | `EvictionPolicy` subclass |
| Buffer manager | `RedisDatabase::enforceCapacityLocked()` |
| Disk | The `dump.my_rdb` snapshot file |
| Write-back | Clearing the dirty bit after a snapshot |

Two things follow, and they are why the project measures what it measures:

1. **Hit/miss rate measures request behaviour.** Same as in a database.
2. **`write_backs` measures I/O.** Only a dirty page that gets evicted (or
   written back at snapshot time) costs real I/O. This is the metric a genuine
   buffer-management study compares — and it is exposed in `INFO`.

### Capacity is in **keys**, not bytes

`CacheEntry::byteSize` is populated for every key
(`RedisDatabase.cpp:36-52`) and reported via `MEMORY USAGE` / `INFO`, but
**nothing enforces a byte ceiling**. `MAXKEYS` is the only real limit. This is a
deliberate simplification (`docs/Project/Architecture.md:87-97`); byte-bounded
enforcement is listed as future work (P1.3).

---

## 4. The Eviction Policy Library

This is the heart of the project. Five policies sit behind one interface, and
the server, the unit tests, and offline replay all instantiate the same objects.

### 4.1 The `EvictionPolicy` interface

`Redis-Server/include/eviction/eviction_policy.h:12-38`

```cpp
class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;
    virtual void onInsert(const std::string& key)                       = 0;  // :16
    virtual void onAccess(const std::string& key)                       = 0;  // :17
    virtual void onRemove(const std::string& key)                       = 0;  // :18
    virtual void onReplace(const std::string& key) {}                        // :23
    virtual std::string selectVictim() const                             = 0;  // :27
    virtual void registerEvictionDecision(const std::string& victim) {}        // :32
    virtual void syncKeys(const std::vector<std::string>& keys)           = 0;  // :35
    virtual std::string name() const                                     = 0;  // :37
};
```

Two design points worth naming:

**It carries no capacity, no payload, no clock** (`:7-11`). A policy only sees
*keys* — never values, never the store. That is what lets the identical object
serve the live server, the unit tests, and offline replay against a recorded
trace.

**Two methods are `virtual` with a no-op default**, and both exist for reasons:

- `onReplace(key)` (`:23`) — a value rewrite is **neither a read nor a new
  arrival**. If it were treated as an access, overwriting a hot key would
  artificially refresh its recency and inflate its frequency. Default no-op;
  policies that care override it.
- `registerEvictionDecision(selectedVictim)` (`:32`) — tells the policy that the
  owner *acted* on the choice. This lets a policy learn from the candidate it
  did **not** get. This is the hook the entire adaptive controller hangs off.

### 4.2 `CacheEntry` — the frame descriptor

`Redis-Server/include/eviction/cache_entry.h:13`

```cpp
struct CacheEntry {
    std::string     key;                 // :14  page number
    std::uint64_t   insertionTime{0};    // :18  logical clock, NOT wall clock
    std::uint64_t   lastAccessTime{0};   // :19
    std::uint64_t   accessCount{0};      // :20
    std::size_t     byteSize{0};         // :23  approximate footprint (advisory)
    bool            dirty{false};        // :27  modified since last write-back
};
```

**Timestamps are logical clock ticks, not wall-clock time** (`:16-17`). The
database owns a single monotonically increasing `logicalClock_`
(`RedisDatabase.h:119`), bumped on every insert and access. This makes replays
and unit tests deterministic — a policy can never depend on how fast the machine
is or when the test happened to run.

**Absent by design:** pin count, LSN. Both are noted in
`docs/DBMS/Buffer Pool.md:71-77` as things a real buffer manager has and this
one does not.

### 4.3 `EvictionMetrics` — the counters

`Redis-Server/include/eviction/eviction_metrics.h:11`

```cpp
struct EvictionMetrics {
    uint64_t totalAccesses, totalHits, totalMisses, totalKeysInserted;  // :12-15
    uint64_t evictions, writes, cleanEvictions;                          // :17-19
    uint64_t lruSelections, lfuSelections, fifoSelections,
             randomSelections, adaptiveSelections;                       // :21-25
    uint64_t expirations, explicitDeletes, policyChanges,
             capacityEnforcements;                                       // :27-30
    uint64_t hitRatePercent() const;                                     // :32-36
    void reset();                                                       // :38
};
```

Hit rate is **integer division**, which truncates:

```
hitRatePercent = (totalHits * 100) / (totalHits + totalMisses)
```

Guarded to `0` when the denominator is zero. `testEngineCapacityAndMetrics`
asserts exactly this: 2 hits / 1 miss → `66` (`test_eviction.cpp:298`).

The header comment (`:5-10`) states the intent: hits/misses describe *request
behaviour*; `writes` describes *the I/O a buffer pool would incur*. "Those two
are the numbers a buffer-management study actually compares."

### 4.4 The `EvictionEngine` — factory + metrics hub

`Redis-Server/src/eviction/eviction_engine.cpp` — the seam between the storage
engine and the policy library. It is **not internally synchronized** (`eviction_engine.h:15-16`);
the owning `RedisDatabase` serializes everything under `db_mutex`.

| Function | Line | Behaviour |
|---|---|---|
| `makePolicy(name)` | `:22-29` | factory: `std::make_unique<LRUPolicy>()` etc. |
| `EvictionEngine()` | `:33-36` | **defaults to `ADAPTIVE`** |
| `policyNames()` | `:40-42` | `{"LRU","LFU","FIFO","RANDOM","ADAPTIVE"}` |
| `canonicalPolicyName(name)` | `:44-50` | uppercase; `CACHEUS` → `ADAPTIVE`; `""` if unknown |
| `isKnownPolicy(name)` | `:52-54` | case-insensitive |
| `setPolicy(name)` | `:56-70` | swaps the object, bumps `policyChanges`. **The new policy starts EMPTY** — the caller must `resync()` (`:63-65`) |
| `setMaxKeys(keys)` | `:72` | `0` disables enforcement |
| `onInsert` | `:74-77` | delegate + `++totalKeysInserted` |
| `onAccess` | `:79-82` | delegate + `++totalAccesses` |
| `onReplace` | `:84-86` | counts as an access; **does not** forward to `policy_->onReplace()` (see [§11](#11-bugs-found-and-fixed)) |
| `selectVictim` | `:92` | pure delegation |
| `registerEvictionDecisionFeedback(v)` | `:94-96` | → `policy_->registerEvictionDecision(v)` |
| `resync(keys)` | `:98` | → `policy_->syncKeys(keys)` |
| `recordHit()` | `:100-103` | `++totalHits`, `++totalAccesses` |
| `recordMiss()` | `:105-108` | `++totalMisses`, `++totalAccesses` |
| `recordEviction(dirty)` | `:110-115` | `++evictions`; `++writes` if dirty, else `++cleanEvictions`; then attribute to the active policy |
| `bumpPolicySelection()` | `:123-129` | increments whichever per-policy counter matches the active policy |
| `policyDetail()` | `:131-145` | `dynamic_cast` to `AdaptivePolicy`; returns `""` for static policies |

`policyDetail()` emits the single line that makes the controller observable:

```
lru_weight=0.312 lfu_weight=0.688 lru_shadow_loss=2.000 lfu_shadow_loss=5.000 adaptive_decisions=412 preferred_expert=LFU
```

(precision 3). The UI parses these straight out of `INFO`.

### 4.5 LRU

`Redis-Server/src/eviction/lru_policy.cpp`

```cpp
std::list<std::string> order_;                       // front = MRU, back = LRU   (.h:21)
std::unordered_map<std::string, std::list<std::string>::iterator> pos_;         // (.h:22)
```

- **`touch(key)`** (`.cpp:3-11`) — erase from list + map if present, then
  `push_front` and record the iterator. **O(1)** splice-style update.
- **`selectVictim()`** (`.cpp:23-25`) — `order_.back()`. **O(1)**.
- **`syncKeys()`** (`.cpp:27-43`) — builds an `unordered_set wanted` **once**
  (`:30`), then makes two passes. The comment at `:28-29` records a fixed bug:
  the previous version rescanned the full key list for each tracked key, making
  the bulk reconcile **quadratic**.

### 4.6 LFU

`Redis-Server/src/eviction/lfu_policy.cpp`

```cpp
struct Meta { uint64_t frequency{0}; uint64_t lastAccess{0}; };  // .h:26-29
std::unordered_map<std::string, Meta> meta_;                    // .h:31
uint64_t logicalClock_{0};                                       // .h:32
```

- **`onInsert`** (`.cpp:4-10`) — **early-returns if the key is already tracked**
  (`:7`). Writing a value over an existing key must not reset a hot key's
  counter. Asserted by `testLfuKeepsFrequencyAcrossReinsert` (`test_eviction.cpp:83`).
- **`onAccess`** (`.cpp:12-21`) — unknown key delegates to `onInsert` (`:14-17`);
  otherwise bump the clock, `++frequency`, `lastAccess = clock`.
- **`selectVictim`** (`.cpp:27-45`) — **linear scan** for the lowest frequency,
  **older `lastAccess` wins ties** (`:37-38`). **O(n)** — the asymmetry against
  LRU's O(1) is deliberate and documented (`lfu_policy.h:11-15`).

### 4.7 FIFO

`Redis-Server/src/eviction/fifo_policy.cpp`

```cpp
std::deque<std::string> queue_;           // .h:19
std::unordered_set<std::string> present_; // .h:20
void trimFront();                          // .h:24
```

- **`onInsert`** (`.cpp:3-5`) — `push_back` only if `present_.insert()` returned
  true (dedup).
- **`onAccess`** (`.cpp:7-9`) — **intentionally empty**. FIFO ignores access
  history; that is the whole point. Asserted by `testFifoIgnoresAccess`
  (`test_eviction.cpp:128`).
- **`trimFront()`** (`.cpp:16-19`) — pops leading tombstones so the deque does not
  grow without bound as keys are individually deleted.
- **`selectVictim`** (`.cpp:21-26`) — scans forward past tombstones to the first
  still-present key. **O(n)**.
- **`syncKeys`** (`.cpp:28-43`) — inserts all, prunes `present_`, then compacts
  **both** head and tail (`:39-42`).

### 4.8 RANDOM

`Redis-Server/src/eviction/random_policy.cpp`

```cpp
std::vector<std::string> keys_;                    // .h:25  stable draw order
std::unordered_set<std::string> members_;          // .h:26
mutable std::mt19937 rng_;                         // .h:27  mutable → selectVictim() is const
```

- Seeded from `std::random_device{}()` (`.cpp:4`).
- **`selectVictim`** (`.cpp:17-21`) — `uniform_int_distribution` over
  `keys_`. **O(1)**.

RANDOM is not a joke policy. The header (`.h:9-10`) states its purpose:

> "Any policy that cannot beat random under this workload is not learning
> anything about it."

It is the **null model** that gives the other four something to be measured
against.

### 4.9 ADAPTIVE — the research contribution

`Redis-Server/src/eviction/adaptive_policy.cpp` (97 lines) +
`adaptive_policy.h` (73 lines).

**Algorithm: exponential-weights regret minimisation over two experts.**

#### Hyperparameters

| Hyperparameter | Default | Location |
|---|---|---|
| `learningRate_` | **0.20** | `.h:25`, `.cpp:5-6` |
| `lossDecay_` | **0.90** | `.h:25`, `.cpp:5-6` |
| `kMinWeight_` | **0.05** | `.h:58` (`static constexpr`) |
| `weightLRU_` / `weightLFU_` | **0.5 / 0.5** | `.h:60-61` |
| shadow-loss increment | **+1.0** | `.cpp:18`, `.cpp:22` |

#### State

```cpp
LRUPolicy lru_;                        // .h:51   expert 1, always live
LFUPolicy lfu_;                        // .h:52   expert 2, always live

struct PendingCandidate {              // .h:46-49
    std::string key;
    bool        active{false};
};
PendingCandidate pendingLRU_, pendingLFU_;   // .h:63-64  the *shadow* slots

double   lossLRU_{0.0}, lossLFU_{0.0};      // .h:66-67  decayed losses
uint64_t decisions_{0};                     // .h:69
```

#### How it runs, step by step

**(a) Everything is forwarded to both experts.** `onInsert`, `onAccess`,
`onRemove`, `onReplace`, `syncKeys` all call through to *both* `lru_` and `lfu_`
(`.cpp:8-45`, `:94-97`). Because they track the same key set in lockstep, the
resident set stays coherent and **exactly one key is ever really evicted**.

**(b) Each expert nominates a candidate.** When the owner is about to evict, it
calls `registerEvictionDecision`. The controller snapshots both experts'
candidates *before* anything is removed (`.cpp:64-67`):

```cpp
pendingLRU_.key = lru_.selectVictim();
pendingLRU_.active = !pendingLRU_.key.empty();
pendingLFU_.key = lfu_.selectVictim();
pendingLFU_.active = !pendingLFU_.key.empty();
```

One of these is used. The other is left behind as a **shadow**.

**(c) The shadow becomes the loss signal.** This is the clever part
(`.cpp:13-25`):

```cpp
void AdaptivePolicy::observeAccessLoss(const std::string& key) {
    if (pendingLRU_.active && pendingLRU_.key == key) {
        lossLRU_ += 1.0;
        pendingLRU_.active = false;
    }
    if (pendingLFU_.active && pendingLFU_.key == key) {
        lossLFU_ += 1.0;
        pendingLFU_.active = false;
    }
}
```

Called at the top of `onAccess` (`.cpp:28`). **If a shadow candidate is
requested again shortly after it nearly got evicted, that expert had the ordering
wrong.** No ML model, no gradient, no dataset — just "you almost threw away
something that was needed, and here's proof."

**(d) Weights are updated multiplicatively** (`.cpp:59-82`):

```cpp
lossLRU_ *= lossDecay_;                          // :72   forget older evidence
lossLFU_ *= lossDecay_;                          // :73

const double minLoss   = std::min(lossLRU_, lossLFU_);   // :75
const double regretLRU = lossLRU_ - minLoss;             // :76
const double regretLFU = lossLFU_ - minLoss;             // :77

weightLRU_ *= std::exp(-learningRate_ * regretLRU);     // :79
weightLFU_ *= std::exp(-learningRate_ * regretLFU);     // :80
normalizeWeights();                                        // :81
```

As formulas:

```
decay        loss_e     ← 0.90 · loss_e
regret       regret_e   = loss_e − min(loss_LRU, loss_LFU)
multiplier   w_e        ← w_e · exp(−0.20 · regret_e)
```

**The min-subtraction is what makes this relative.** Only the *gap* between the
two experts drives the update — not the absolute magnitude of the losses. Both
experts having a bad round changes nothing; one being worse than the other does.

**(e) Weights are floored then normalized** (`.cpp:47-57`):

```cpp
weightLRU_ = std::max(weightLRU_, kMinWeight_);   // :48
weightLFU_ = std::max(weightLFU_, kMinWeight_);   // :49
const double total = weightLRU_ + weightLFU_;      // :50
if (total <= 0.0) { weightLRU_ = weightLFU_ = 0.5; return; }
weightLRU_ /= total;  weightLFU_ /= total;
```

The floor is applied **before** normalization, so two invariants always hold:

```
w_LRU + w_LFU == 1        (exactly)
w_LRU ≥ 0.05  and  w_LFU ≥ 0.05
```

The floor is why the controller can recover from a workload shift: a "dead"
expert that hit zero could never climb back, and the system would stop adapting
permanently. `testAdaptiveAdaptsAndKeepsFloor` (`test_eviction.cpp:182`) runs
500 rounds and asserts both invariants to 1e-9.

**(f) The victim is chosen by hard argmax** (`.cpp:84-92`):

```cpp
const std::string lruVictim = lru_.selectVictim();
const std::string lfuVictim = lfu_.selectVictim();
if (lruVictim.empty()) return lfuVictim;
if (lfuVictim.empty()) return lruVictim;
return (weightLRU_ >= weightLFU_) ? lruVictim : lfuVictim;   // :91
```

This is an **argmax, not a blend**. Documented as an accepted limitation
(`docs/DBMS/Adaptive Replacement.md:139-143`): with two discrete experts and a
hard comparison, the effective behaviour is always *exactly* "pure LRU" or "pure
LFU" — never something in between. The adaptivity lives in **which** of the two
rules is being followed, not in a weighted combination of them.

#### Ordering constraint (critical)

`registerEvictionDecisionFeedback()` **must** be called **before**
`removeKeyLocked()` — `RedisDatabase.cpp:122-125`, and called out in
`eviction_policies/README.md:50-56`. After the key is removed the two experts no
longer agree on the resident set, so the shadow candidate would already have been
erased by `onRemove` (which clears `active`, `.cpp:36-37`) and the round's
evidence would be silently lost.

#### What this is *not* (the honest feature matrix)

`docs/DBMS/Adaptive Replacement.md:41-54`:

| CacheUS feature | Present here |
|---|---|
| Sliding access window | ❌ |
| Segmented cache (Window / Probationary / Protected) | ❌ |
| Ghost cache | ❌ |
| ML cost model (logistic regression) | ❌ |
| Online gradient training | ❌ |
| Adaptive sampling rate | ❌ |
| **Shadow evaluation of an alternative** | ✅ |
| **Weight update from observed loss** | ✅ |

The project states plainly that it is CacheUS-*inspired*, not CacheUS. That
discipline is what makes the rest of the documentation trustworthy.

---

## 5. The C++ Server

### 5.1 `RedisDatabase` — storage + buffer manager

`Redis-Server/src/RedisDatabase.cpp` (1015 lines) +
`Redis-Server/include/RedisDatabase.h` (137 lines).

#### Core state

```cpp
enum class OpStatus { Ok, NotFound, WrongType };              // .h:17-21
const char* statusError(OpStatus);                            // .h:24

struct Value {                                                // .h:104-110
    enum class Type { String, List, Hash };
    Type        type{Type::String};
    std::string str;
    std::vector<std::string> list;
    std::unordered_map<std::string, std::string> hash;
};

mutable std::mutex db_mutex;                                          // :114
std::unordered_map<std::string, Value>                store;          // :115
std::unordered_map<std::string, CacheEntry>           frames;         // :116
std::unordered_map<std::string, Clock::time_point>   expiry_map;     // :117
EvictionEngine                                          engine;        // :118
std::uint64_t                                          logicalClock_{0}; // :119

enum class RemovalReason { Explicit, Expired, Evicted };  // :122
```

A function-local static singleton via `getInstance()` (`.cpp:18-21`).

**One keyspace, one type per key.** The header comment (`.h:101-103`) records
the reason: an earlier design used three parallel maps, letting one key hold a
string *and* a list *and* a hash at once — which produced duplicate `KEYS`
results and meaningless capacity accounting.

`db_mutex` is `mutable` so read-only accessors can still lock.

#### The `...Locked()` convention

Every internal helper that touches state carries a `Locked` suffix
(`.h:124-134`):

```
purgeExpiredLocked  removeKeyLocked   enforceCapacityLocked
registerInsertLocked registerAccessLocked resyncPolicyLocked
valueBytesLocked     typeIsLocked
```

This is enforced by **naming**, and it exists to prevent a real bug: a
non-recursive `std::mutex` self-deadlocks if a locked path calls a public method
that re-locks. It already happened once — `configGet()` called the public
`totalKeyBytes()` (`docs/OS/Mutual Exclusion.md:80-83`).

#### The buffer manager — `enforceCapacityLocked()`

`RedisDatabase.cpp:110-129`. This is the function that *is* page replacement:

```cpp
if (!engine.capacityLimited()) return;                                    // :111

while (store.size() > engine.maxKeys()) {                                 // :115
    const std::string victim = engine.selectVictim();                      // :116
    if (victim.empty()) break;                                            // :117

    const bool dirty = frames.count(victim) ? frames[victim].dirty : false; // :119

    engine.registerEvictionDecisionFeedback(victim);  // :123  ← BEFORE removal
    removeKeyLocked(victim, RemovalReason::Evicted);  // :125
    engine.recordEviction(dirty);                      // :126
    engine.onCapacityEnforced();                       // :127
}
```

Three details:

- The control flow is a **`while`, not an `if`** (`:113-114`), so lowering
  `MAXKEYS` at runtime drains the excess immediately rather than one key per
  insert. `testShrinkingCapacityEvictsImmediately` (`test_integration.cpp:374`)
  covers this.
- `registerEvictionDecisionFeedback` precedes `removeKeyLocked` (see
  [§4.9](#49-adaptive--the-research-contribution)) — required for the adaptive
  controller's shadow to survive.
- `recordEviction(dirty)` splits evictions into `writes` (dirty → real I/O) and
  `cleanEvictions` (no I/O).

#### Registration paths

`registerInsertLocked()` (`.cpp:59-80`):

- **New key** → create `CacheEntry{insertionTime = logicalClock_++,
  lastAccessTime = insertionTime, accessCount = 0, byteSize, dirty = true}` and
  call `engine.onInsert(key)`.
- **Existing key** → update `byteSize`, set `dirty = true`, and call
  **`engine.onReplace(key)`** with the comment "A rewrite is not a read" (`:76`).
- Only if `isNew`, call `enforceCapacityLocked()` (`:79`).

`registerAccessLocked()` (`.cpp:82-88`): `lastAccessTime = logicalClock_++`,
`++accessCount`, `engine.onAccess(key)`.

`resyncPolicyLocked()` (`.cpp:90-108`): builds `vector<pair<insertionTime, key>>`
and **sorts ascending (oldest first)** before handing it to the policy. The
comment at `:91-93` explains why: without the sort, FIFO would resume in hash
order and immediately evict keys that had been there longest.

`purgeExpiredLocked()` (`.cpp:148-163`): **collect-then-remove**. The comment
records that ASan caught a use-after-free from erasing while iterating
`expiry_map`.

#### Full function map

| Function | Lines | Notes |
|---|---|---|
| `statusError` | `:9-16` | `"no such key"` / `"wrongtype operation against a key holding the wrong kind of value"` |
| `typeName` | `:23-30` | `string` / `list` / `hash` / `none` |
| `valueBytesLocked` | `:36-52` | string size; list `sizeof(string)*capacity()` + items; hash per-field. **Reported, never enforced** |
| `typeIsLocked` | `:54-57` | type guard |
| `registerInsertLocked` | `:59-80` | see above |
| `registerAccessLocked` | `:82-88` | see above |
| `resyncPolicyLocked` | `:90-108` | see above |
| **`enforceCapacityLocked`** | `:110-129` | **the buffer manager** |
| `removeKeyLocked` | `:131-146` | erases store/frames/expiry; `engine.onRemove`; counts explicit deletes and expirations |
| `purgeExpiredLocked` | `:148-163` | lazy TTL sweep |
| `globMatch` | `:172-191` | Redis glob: `*` = any run, `?` = one char, with `star`/`mark` backtracking |
| `setPolicy` | `:197-204` | lock → purge → `engine.setPolicy` → `resyncPolicyLocked` |
| `setMaxKeys` | `:211-216` | sets the limit **and immediately enforces** — lowering drains at once |
| `dbSize` / `totalKeyBytes` / `memoryUsage` | `:223-239` | introspection |
| `configGet` | `:247-260` | `POLICY`\|`EVICTION`, `MAXKEYS`, `DBSIZE`, `BYTES` |
| **`infoText`** | `:262-305` | 20 `key:value` lines under `# Server`, `# Cache`, `# Access`, `# Eviction` + optional `adaptive_detail:` |
| `flushAll` | `:311-318` | clears everything, then `engine.resync({})` |
| `set` | `:324-336` | `registerInsertLocked(key, key.size()+value.size(), isNew)` |
| `get` | `:338-356` | **`recordMiss()` on not-found *or* wrong type**; otherwise `registerAccessLocked` + `recordHit()` |
| `keys` | `:358-372` | full scan; **calls `engine.onAccess()` on every matched key** (`:368`) |
| `expire` / `ttl` / `persist` / `rename` | `:393-455` | `ttl` returns `-2` missing, `-1` no-expiry, else seconds. `rename` moves the frame, notifies `onRemove`/`onInsert`, and **calls `enforceCapacityLocked()`** (`:453`) because RENAME overwrites the destination |
| List ops | `:461-652` | `lget llen lpush rpush lpop rpop lrem lindex lset`. `lrem` handles `count == 0` (all), `> 0` (head→tail), `< 0` (tail→head, `:584-598`). **Empty list → key removed** (`:535`, `:555`, `:605`) |
| Hash ops | `:658-801` | `hset hget hexists hdel hgetall hkeys hvals hlen hmset`. `HSET` returns count of *newly created* fields. **Empty hash → key removed** (`:723`) |
| `writeChunk` / `readLength` / `readChunk` | `:833-858` | see [§5.4](#54-persistence) |
| `dump` | `:862-930` | snapshot writer |
| `load` | `:932-1015` | snapshot reader |

`get()` treating a wrong-type read as a **miss** (`:338-356`) is a defensible
cache-layer choice: from the buffer pool's point of view the frame did not
satisfy the request.

`keys()` refreshing every matched key's recency/frequency (`:368`) means a
`KEYS *` makes the whole keyspace look hot — but it does **not** increment
`totalHits`. Documented in [§11](#11-bugs-found-and-fixed) as a known quirk
rather than an oversight.

### 5.2 `RedisServer` — transport and threading

`Redis-Server/src/RedisServer.cpp` (248 lines).

```cpp
constexpr std::size_t kReadChunk       = 8192;                    // :23
constexpr std::size_t kMaxPendingBytes = 16u * 1024u * 1024u;    // :24  16 MB/conn
```

| Function | Lines | Notes |
|---|---|---|
| `signalHandler(int)` | `:29-32` | **async-signal-safe only** — no allocation, no iostreams, no `exit()`; just `globalServer->requestShutdown()` |
| `sendAll(fd, data)` | `:35-47` | resumes on `EINTR` and short writes; uses `MSG_NOSIGNAL` (`:38`) |
| **`serveClient(fd, running)`** | `:52-98` | **a free function**, not a member (`:49-51`) — detached threads must not dereference a `RedisServer` that may already be destroyed |
| ctor | `:102-112` | sets `globalServer`, installs the signal handler |
| `setupSignalHandler()` | `:114-124` | `SIGPIPE` → `SIG_IGN` (`:117`); `SIGINT`/`SIGTERM` via `sigaction` with **`sa_flags = 0`, deliberately no `SA_RESTART`** so a blocked `accept()` returns (`:121`) |
| `requestShutdown()` | `:126-130` | `running.store(false)` + `::shutdown(listen_fd, SHUT_RDWR)` to unblock `accept()` |
| `bindSocket()` | `:132-162` | `socket` → `SO_REUSEADDR` (`:140`) → `bind` with `htons()` (`:144`) → `listen(sock, 128)` (`:153`) |
| `persistNow()` | `:164-171` | one-shot snapshot |
| `startPersistence()` | `:173-194` | snapshot thread; **sleeps in 200 ms slices** (`:180`) so shutdown is noticed promptly; elapsed time accumulated across slices (`:182-189`) |
| `stopPersistence()` | `:196-199` | clears the flag, `join()`s |
| **`run()`** | `:201-243` | bind → start persistence → print policy/capacity → **accept loop spawning detached `std::thread(serveClient, ...)`** (`:227`) → 100 ms grace sleep (`:231`) → stop persistence → close socket → **final `persistNow()`** (`:241`) |
| `shutdown()` | `:248` | thin wrapper; `run()` owns the final snapshot to avoid double-dumping |

#### The client loop

`serveClient` (`:52-98`):

1. `recv()` into an 8 KB chunk, append to a `pending` buffer.
2. Enforce the 16 MB per-connection ceiling (`:67-71`) — a client that never
   completes a command cannot exhaust server memory.
3. **Drain every complete command** in the buffer with `consumed` accounting
   (`:78-91`) — this is what makes pipelining work.
4. `pending.erase(0, offset)` — the tail may hold a partial command.

#### Threading model

`docs/Project/Architecture.md:103-107`:

| Thread | Created in | Lifetime | Synchronization |
|---|---|---|---|
| Main / accept loop | `main()` | process | — |
| Client handler | per `accept()` | connection, **detached** | `db_mutex` |
| Snapshot | `run()` | process, **joined** | `db_mutex`, `persistRunning` |

`Redis-Server/src/main.cpp:37-72`: parses `[port=6379] [dump-file="dump.my_rdb"]
[snapshot-interval=300]`, validates `1 ≤ port ≤ 65535` (`:57-60`), loads any
existing snapshot (`:64-69`), constructs `RedisServer`, calls `run()`.
**Policy and capacity are not CLI flags** — they are set at runtime via
`CONFIG SET`.

### 5.3 `RedisCommandHandler` — the RESP protocol

`Redis-Server/src/RedisCommandHandler.cpp` (618 lines).

```cpp
enum class ParseResult { NeedMore, Complete, Invalid };                    // :9-13
ParseResult parseRespCommand(const std::string& buf, std::size_t& consumed,
                             std::vector<std::string>& tokens);           // :19-20
std::string processCommand(const std::string& buf, std::size_t& consumed,
                           bool& closeConnection);                         // :29-30
```

**`consumed` is the whole design** (`docs/Project/RESP Protocol.md:46-52`). The
previous signature returned only tokens, so pipelining was impossible, partial
commands were silently discarded, and commands over 1024 bytes were truncated.

Wire bounds (`:32-33`):

```cpp
constexpr long long kMaxMultibulkElements = 1024 * 1024;        // 1M
constexpr long long kMaxBulkLength         = 512LL * 1024 * 1024; // 512 MB
```

#### `parseRespCommand` (`:60-127`)

- A `reject()` lambda (`:69-72`) sets `consumed = buffer.size()` and returns
  `Invalid`. **`consumed` must be non-zero** or the read loop spins forever on a
  permanently bad buffer.
- **Inline path** (`:74-90`): find `'\n'`, strip the trailing `\r`, tokenize on
  whitespace. This is what makes `redis-cli` typing `PING\r\n` directly work.
- **Multibulk path** (`:92-126`): `readLine` the count, loop over `$len` bulks;
  if `pos + length + 2 > buffer.size()` return `NeedMore`.

The comment at `:98-99` records a severe bug: an early version did
`header.substr(1)` to strip a prefix that `readLine` had *already* consumed,
dropping the first digit of every count and every length. Every `redis-cli`
command failed with `-ERR protocol error`.

#### Reply helpers (`:135-154`)

`simpleReply` `+` · `errorReply` `-ERR ` · `nilReply` `$-1` ·
`integerReply` `:` · `bulkReply` `$len` · `bulkArray` `*n`

#### Dispatch table (`:560-618`)

| Group | Commands |
|---|---|
| Common | `PING` `ECHO` `FLUSHALL` `QUIT` (`:574-577`) |
| Strings / KV | `SET` (with `EX`/`PX`/`NX`/`XX`, `:186-222`) `GET` `KEYS` `TYPE` `DEL` `UNLINK` `EXISTS` `EXPIRE` `TTL` `PERSIST` `RENAME` |
| Lists | `LGET` `LLEN` `LPUSH` `RPUSH` `LPOP` `RPOP` `LREM` `LINDEX` `LSET` |
| Hashes | `HSET` `HGET` `HEXISTS` `HDEL` `HLEN` `HKEYS` `HVALS` `HGETALL` `HMSET` |
| Admin | `INFO` (`:469-471`) `DBSIZE` `MEMORY USAGE` `EVICTION` (`:504-508`) `MAXKEYS` (`:510-515`) `CONFIG GET\|SET\|RESETSTAT` (`:517-546`) |

All names are upper-cased for case-insensitivity (`:571`). `DEL` and `UNLINK`
share `handleDel` (`:583`).

`SET ... PX` **rounds up** to whole seconds: `(amount + 999) / 1000` (`:203`).
Using `PX` with 1500 ms yields a 2-second TTL, not 1.

#### Why these "non-standard" commands exist

The comment at `:461-467` (and `docs/Project/RESP Protocol.md:144-149`) is
blunt: `benchmark_client.py` and `ui_app.py` were **already issuing** `INFO`,
`CONFIG SET POLICY`, `CONFIG SET MAXKEYS`, `CONFIG RESETSTAT`, `EVICTION`, and
`MAXKEYS` — and the server implemented none of them. Benchmark runs "succeeded"
while measuring nothing. The commands were added to the server to match the
clients.

### 5.4 Persistence

`RedisDatabase.cpp:806-858`, `:862-1015`.

#### On-disk format

```
<ttl> K <keylen> <key>                          <vallen> <value>
<ttl> L <keylen> <key>     <count> (<itemlen> <item>)*
<ttl> H <keylen> <key>     <count> (<fieldlen> <field> <vallen> <value>)*
```

`<ttl>` is seconds remaining, `-1` for none. **Every field carries an explicit
byte length**, which makes arbitrary binary keys and values safe — unlike a
whitespace-delimited format. The trade-off is documented in
`docs/Project/Architecture.md:87-97`: length-prefixed text is binary-safe and
human-readable, but it is **not** a slotted page file, and it is not
space-reclaimable.

Safety ceilings (`:830-831`):

```cpp
constexpr std::size_t kMaxRecordsPerKey = 10'000'000;
constexpr std::size_t kMaxRecordBytes   = 512u * 1024u * 1024u;  // 512 MB
```

#### `readChunk` — the one-byte trap

`RedisDatabase.cpp:844-858`. After reading the length it consumes **exactly one**
separator character (`:851-852`). Reading more would eat leading spaces from
the payload. The recorded bug: an earlier version read `" user:"` instead of
`"user:1"` — the length was read, then the separator was skipped twice, silently
corrupting every value that started with a space.

#### `dump()` — atomic, and it clears dirty bits

`RedisDatabase.cpp:862-930`:

1. Write to `<filename>.tmp`
2. `flush()`
3. Verify
4. `remove()` the old file
5. `rename()` the temp into place — **atomic on POSIX** (`:914-927`)
6. **Clear every frame's `dirty` bit** (`:928`)

Step 6 is what makes `write_backs` meaningful. Once a snapshot has captured a
frame, that frame is clean; evicting it later costs no I/O.

#### `load()` — staged, then swapped

`RedisDatabase.cpp:932-1015`:

1. Parse into **staging** containers `stagedStore` / `stagedExpiry` (`:941-942`)
2. Reject the whole snapshot on any malformed record or unknown tag
3. Require at least one record was parsed (`parsedAny`, `:994`) — an empty file
   is corruption, not "an empty database"
4. `swap()` staging into the live containers (`:996-997`)
5. Clear `frames`, resync the policy
6. **Rebuild frames** with fresh `insertionTime = logicalClock_++` and
   `dirty = false` (`:1002-1011`) — restored keys are clean and "recently
   inserted", because the process genuinely has no memory of when they were
   written
7. `enforceCapacityLocked()` (`:1012`)

Staging means a corrupt snapshot can never leave the database half-populated.
`testCorruptSnapshotIsRejected` (`test_integration.cpp:494`) asserts exactly this.

A real sample from the committed `dump.my_rdb`:

```
-1 K 14 yf:NVDA:latest 55 {"Date":"2026-10-16","Close":214.13,"Volume":120855100}
-1 K 24 yf:NVDA:close:2026-10-14 54 {"Date":"2026-10-14","Close":221.3,...}
```

---

## 6. The Python Layer

### 6.1 `redis_client.py`

A dependency-free RESP2 client (260 lines) shared by the benchmark and the UI.

```python
POLICIES = ["LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"]        # :18

class RedisRESPError(RuntimeError): ...                        # :21
def encode_command(parts) -> bytes: ...                        # :25-43

class RedisRESPClient:
    def __init__(self, host="127.0.0.1", port=6379, timeout=3.0)   # :49
```

| Member | Lines | Behaviour |
|---|---|---|
| `encode_command` | `:25-43` | only `bytes`/`bytearray` pass through raw; everything else `str().encode()`. Passing an `int` through `bytes()` would send NUL bytes instead of the decimal number (`:29-31`) |
| `_readline` / `_readexact` / `_read_reply` | `:86-124` | handles `+ - : $ *`; negative `$` length → `None`; negative `*` count → `None`; `recv(65536)` chunks |
| `command(*parts, _retry=True)` | `:126-149` | **one automatic reconnect-and-retry on transport failure** (`:140-149`) — safe because every exposed command is idempotent |
| `info()` | `:156-170` | parses `INFO`, skips `#` headers, coerces numerics |
| `eviction` / `set_policy` / `maxkeys` / `set_maxkeys` / `config_get` / `reset_stats` / `flushall` | `:182-201` | cache controls |
| `set` `get` `delete` `keys` `key_type` `ttl` `exists` `hgetall` `lget` | `:205-234` | data ops |
| `describe(key)` | `:238-248` | → `{key, type, ttl, bytes}` or `{}` |
| `describe_many(keys, limit=200)` | `:250-260` | batches; each key costs three round trips (`TYPE`, `TTL`, `MEMORY USAGE`) |

### 6.2 `ui_app.py`

A 744-line Streamlit control panel — a purpose-built replacement for
RedisInsight/ARDM, which cannot connect to this server (see below).

#### Why not an existing Redis GUI

`CONTROL_PANEL_GUIDE.md:7-21`: RedisInsight and ARDM both open a connection by
issuing `HELLO` to negotiate RESP3 and immediately receive
`-ERR unknown command 'HELLO'`. They also require `COMMAND DOCS`, `CLIENT LIST`,
`CONFIG GET *`, `CLUSTER INFO`, and `SCAN` — none of which are implemented. A
custom client for a custom server.

#### Structure

```python
REPO_ROOT      = Path(__file__).resolve().parent                 # :29
DEFAULT_RESULTS  = REPO_ROOT/"Benchmark"/"results"               # :30
DEFAULT_ANALYSIS = REPO_ROOT/"Benchmark"/"analysis_output"      # :31
HISTORY_LIMIT  = 120                                             # :33
```

| Function | Lines | Role |
|---|---|---|
| `_client(host, port)` | `:42-45` | `@st.cache_resource` — one client per (host, port) across reruns |
| `client_for()` | `:48-55` | connects lazily |
| `try_call()` | `:58-67` | catches `RedisRESPError`/`OSError` → `st.error` + a default. Never a stack trace |
| `safe_info()` | `:70-74` | `{}` on failure |
| `require_server()` | `:77-92` | PING probe; prints **the exact command to start the server** if unreachable |
| `push_history()` | `:95-107` | appends `{t, keys, hit_rate, evictions, hits, misses}`, trims to 120 |
| `fmt_bytes()` | `:115-123` | B/KB/MB/GB |
| **`render_dashboard_body()`** | `:130-205` | 6 metrics (`:142-156`), fill bar (`:158-162`), 5 more metrics (`:164-170`), 2 live line charts (`:172-181`), **victim-selection bar chart** (`:183-197`), **adaptive controller expander** parsing `adaptive_detail` (`:199-205`) |
| **`tab_control()`** | `:212-269` | policy selectbox + Apply; capacity `number_input` (0–1,000,000) + Apply; Reset statistics; **FLUSHALL behind a confirm checkbox** (`:265`) |
| **`tab_keys()`** | `:276-316` | glob pattern, max keys 10–500, per-type counts, TTL labels (`—` / `missing` / `Ns`), inspect selectbox |
| `show_key_detail()` | `:319-364` | string → editable textarea + Save/Delete; list → indexed table; hash → field/value table |
| `render_eviction_body()` | `:371-388` | live fragment body |
| **`tab_eviction()`** | `:391-442` | the **eviction lab**: capacity 2–100, write 3–200 keys, tracked key `evict:00`; writes `evict:{i:02d}` (`:411-422`), checks survival via `client.exists()` (`:430-439`), live fragment at 1.0 s (`:441`) |
| `aggregate_raw_results()` | `:449-483` | fallback table builder; computes `hit_rate_percent`, `median_latency_ms`, and `p95_latency_ms` via `quantile(0.95)` (`:480`) |
| **`tab_benchmarks()`** | `:493-565` | loads `analysis_summary.csv` if present, else `aggregate_raw_results`; three expandable bar charts — hit rate / evictions / P95 (`:547-558`); warns if server evictions are all-NaN (`:560-565`) |
| `fetch_history(ticker, period)` | `:572-586` | `@st.cache_data(ttl=600)`; yfinance `history(interval="1d", auto_adjust=True)` |
| `cache_history()` | `:589-596` | writes `yf:{TICKER}:close:{YYYY-MM-DD}` per day + `yf:{TICKER}:latest` |
| **`tab_stock()`** | `:604-676` | comma tickers (default `AAPL, MSFT, NVDA`), period 1mo–2y, progress bar, Close line chart, latest table, **survival check** (`Resident` / `Evicted / expired`) |
| **`main()`** | `:683-740` | sidebar host/port/Connect, status, Clear history, auto-refresh checkbox, **6 tabs** (`:721-723`): Dashboard · Control · Keys · Eviction lab · Benchmarks · Stock data. Dashboard fragment refreshes at 2.0 s when auto-refresh is on (`:726-727`) |

The **Eviction lab** tab is the demo that makes the whole project click: shrink
capacity to 2, write 10 keys, and watch which of LRU / LFU / FIFO / Random /
ADAPTIVE keeps `evict:00` alive. That is page replacement, made visible in one
browser tab.

`requirements-ui.txt`: `streamlit>=1.46` (needs `st.fragment(run_every=...)` and
`width=`, which replaced the deprecated `use_container_width`; developed against
1.66), `yfinance>=0.2.40`, `pandas>=2.0.0`.

---

## 7. The Benchmark Harness

### 7.1 Workload generation

`Benchmark/generate_stock_workload.py` (51 lines) — **this is the experiment**.

30 tickers (`:6-10`), start time `2026-10-01 09:30:00` (`:12`), each row advances
5–30 s (`:20`), price random-walks by `uniform(-0.5, +0.5)` floored at 5.0
(`:35`). Selection is `random.choices(tickers, weights=weights, k=1)` (`:33`).

**Three deliberate phases** (`:26-31`):

| Rows | Phase | Weighting |
|---|---|---|
| **0–299** | Tech-heavy warmup | **10×** toward `AAPL, NVDA, MSFT, TSLA` |
| **300–699** | Earnings shock | **15×** toward `MRK, CVX, XOM, JPM, BAC` |
| **700–999** | Broad scan | **1×** uniform across all 30 |

Output columns: `Datetime, Ticker, Close` (`:44`).

Row 300 is where the world changes: NVDA and friends go cold while pharma and oil
go hot. Row 700 is equally important — a **uniform** scan tests whether the
controller can *give up* a hot set, not merely acquire a new one. A policy that
only ever learns to hold on is not adaptive; it is sticky
(`docs/Project/Benchmarking.md:31-37`).

### 7.2 `benchmark_client.py`

254 lines.

| Function | Lines | Behaviour |
|---|---|---|
| `percentile(values, p)` | `:15-18` | **nearest-rank**: `ordered[max(0, min(n-1, ceil(p/100*n)-1))]` |
| `command(host, port, text, timeout)` | `:21-46` | **opens a fresh TCP connection per command** (`:24`), sends `text + "\r\n"` inline, reads until a complete reply (with `$`-bulk length awareness, `:34-39`). **Any reply starting with `-` counts as a failure** (`:41-44`) — matching only `-ERR` would score WRONGTYPE/NOSUCHKEY as success |
| `fetch_server_info()` | `:49-64` | `INFO` → typed dict |
| `load_stock_csv(path)` | `:67-86` | tolerant columns: time ∈ {datetime, date, timestamp}, close ∈ {close, adj close, adj_close}, ticker ∈ {ticker, symbol}; skips `nan`/`none`; sanitises tickers to `[A-Za-z0-9._-]` |
| `make_workload()` | `:99-109` | `staleness_seconds = max(0, latest_ts − ts)` per record (`:105`). Synthetic fallback: `T{i:04d}` keys, keyspace `--keyspace` (default 100) |
| `do_op()` | `:112-137` | the operation mix |
| `summarize()` | `:140-163` | 22 output fields |
| `main()` | `:166-252` | CLI + orchestration |

#### The operation mix (`do_op`, `:112-137`)

```python
known = rng.random()*100 < args.hit_target_percent      # default 80.0   :113-114
rec   = records[rng.randrange(len(records))]                                # :115

if known:
    GET stock:<ticker>  /  bench:<ticker>                                   # :118
    if miss:
        SET it
        op = "GET_MISS_THEN_SET"                                            # :121-124
    else:
        op = "GET"                                                          # :126
else:
    GET bench:missing:<worker>:<index>:<seed>     # guaranteed miss        # :128-130
    op = "GET_COLD"
```

This produces a realistic mix: mostly cache hits, occasional misses that pull in
a value (an eviction's downstream cost), and deliberate cold probes that are
guaranteed misses.

`--hit-target-percent` is a **target, not a measurement**. Treating it as the
observed hit rate would be misleading — see [§14](#14-how-to-read-the-results-honestly).

RNG is seeded per worker as `(i+1)*7919 + c*101` (`:227`), so runs are
reproducible per (worker, concurrency).

#### The setup preamble (`main`, `:191-217`)

Order matters:

1. `CONFIG SET POLICY <name>` — accepted set at `:193` includes `CACHEUS`
2. `CONFIG SET MAXKEYS <n>`
3. `FLUSHALL` (unless `--no-flush`)
4. `CONFIG RESETSTAT`
5. **Seed** every known key (`:212-217`) — explicitly *not* counted as measured ops

#### Reported metrics (`summarize`, `:140-163`)

Client-side: `throughput_ops_sec` (**authoritative**, wall-clock, `:152`),
`avg/median/p95/p99/min/max_latency_ms`, `mean_source_data_staleness_seconds`.

Server-side: `eviction_count`, `server_policy`, `server_hits`, `server_misses`,
`server_hit_rate`, `server_keys_stored`, `server_max_keys`.

Server-side numbers are cross-checked against the client's; where they disagree,
the server is authoritative about its own state.

#### CLI

```
--host --port --operations(100) --concurrency(nargs+, [1]) --timeout(2.0)
--output-dir(results) --stock-csv --keyspace(100) --hit-target-percent(80.0)
--label --policy --max-keys --flush/--no-flush --skip-ping
```

Outputs `results/benchmark_{label}_{stamp}.csv` and
`results/summary_{label}_{stamp}.json` containing `{config, server_stats, summaries}`.

### 7.3 `analyze_results.py`

121 lines. Accepts a directory or a single CSV.

1. Validate required columns (`:23`), coerce booleans (`:27-29`).
2. Group by `(run_label, concurrency)`; aggregate count / mean / median /
   `quantile(0.95)` / `quantile(0.99)` / sums (`:32-41`).
3. Hit rate (`:42`): `observed_hit_rate_percent = 100 × hits / (hits + misses)`.
4. **Merge server-side eviction counts** from the matching `summary_*.json`
   (`:45-62`).
5. Approximate throughput from the CSV timestamp span (`:64-69`).
6. Write `analysis_summary.csv` (`:77`) and five charts (`:88-110`, `chart()`
   helper at `:88-101` plots metric vs concurrency, one series per `run_label`):

```
latency_comparison.png   p95_latency_comparison.png   throughput_comparison.png
hit_rate_comparison.png  eviction_comparison.png
```

Plus `analysis_report.md` (`:112-118`).

### 7.4 Batch scripts

| Script | Contents |
|---|---|
| `run_benchmark.bat` | 200 ops, concurrency `1 5 10`, label `baseline` |
| `run_experiments.bat` | **the full 5-policy comparison** on `stock_market_data.csv`, `--max-keys 25 --operations 300 --concurrency 1 5`. Still passes `--policy CACHEUS` for the adaptive run (step 5/5) — exercising the legacy alias path |
| `run_analysis.bat` | thin wrapper |

`Benchmark/requirements.txt`: `pandas>=2.0`, `matplotlib>=3.7`, `tabulate>=0.9`.

---

## 8. The Documentation Vault

`docs/` is an **Obsidian vault** — open `docs/` as the vault root, *not* the repo
root (`docs/README.md:10-16`). 32 notes, ~4,100 lines.

```
docs/
├── Home.md, README.md
├── OS/       (11)  Processes and Threads · POSIX Sockets · Signals and Interrupts
│                  Stream Framing · Mutual Exclusion · Atomics and Memory Ordering
│                  Virtual Memory · File Systems and VFS · System Calls · Clocks
│                  and Time · OS Index
├── DBMS/     (11)  Buffer Pool · Buffer Replacement Policies · Adaptive Replacement
│                  File Organization · Recovery and Logging · Indexing · Query
│                  Processing · Transactions and ACID · Concurrency Control
│                  Normalization · DBMS Index
└── Project/   (8)  Architecture · Eviction Engine · Persistence Format · RESP
                   Protocol · Benchmarking · Concept-to-Code Map · Gaps and
                   Future Work · Project Index
```

#### Status vocabulary

Every note carries a `status` in its frontmatter:

| Status | Meaning |
|---|---|
| `implemented` | present in code, tested, working |
| `partial` | a simplified form exists; the note explains how it differs |
| `not-implemented` | **no code exists** — this is a finding, not a gap |
| `documented-only` | conceptual reference material |
| `reference` | external material |
| `index` | a navigation note |

Current assignments:

- **`partial`** — `OS/Virtual Memory`, `DBMS/Concurrency Control`,
  `DBMS/File Organization`, `DBMS/Indexing`
- **`not-implemented`** — `DBMS/Query Processing`, `DBMS/Recovery and Logging`,
  `DBMS/Transactions and ACID`
- **`documented-only`** — `DBMS/Normalization`

#### Callout convention (`docs/README.md:34-43`)

```
> [!success] Implemented       — present in code, tested, working
> [!warning] Not implemented   — NO CODE EXISTS; this is a finding, not a gap
> [!abstract] Partial          — a simplified form exists; the note says how it differs
```

The rationale (`:54-58`) is worth quoting:

> "This is an academic project, so 'we discussed it' and 'we built it' must not
> be confused."

A `not-implemented` note is a *result*, not a defect. Documents like `Recovery
and Logging` explain that WAL is genuinely absent — that honesty is itself an
academic deliverable.

#### The validator — `tools/validate_vault.py`

186 lines that keep the docs honest.

| Function | Lines | Check |
|---|---|---|
| `collect_notes()` | `:25-27` | `{note_stem: path}` from `docs/**/*.md` |
| `check_frontmatter()` | `:30-50` | requires a leading `---`, a terminated fence, non-empty content, **and both `tags` and `status` keys** (`:47-49`) |
| `link_target()` | `:53-61` | strips `\|alias` and `#heading`; tolerates a trailing `\` (Obsidian escaping inside tables) |
| `check_links()` | `:64-89` | skips links inside fenced code blocks (`:69-75`); accepts both `Note` and `Folder/Note` (`:85`) |
| `check_code_fences()` | `:92-101` | odd fence count → unbalanced |
| `CODE_REF` | `:105` | `` `path.cpp` `` or `` `path.cpp:123` `` |
| `CODE_PATH` | `:107` | bare `src/…`, `include/…`, `tests/…`, `Benchmark/…`, `Redis-Server/…`, `eviction_policies/…` |
| **`check_code_refs()`** | `:110-142` | verifies every referenced path **exists** and every `file:line` is **within the file's actual line count** (`:130-135`) |
| `_resolve_source()` | `:145-156` | tries `repo/rel`, then with/without a `Redis-Server/` or `Benchmark/` prefix |

Exit codes: `0` clean · `1` problems found · `2` vault missing.

Current state — verified live:

```
vault: docs
notes: 32
  DBMS/ 11 notes · OS/ 11 notes · Project/ 8 notes
source references checked: 261
All checks passed.
```

Every one of those **261 `file:line` references is machine-checked against the
actual source.** A doc pointing at a line that no longer exists fails CI-style
validation. That is the mechanism that makes a concept vault trustworthy rather
than decorative.

---

## 9. Build, Run, Test

### Build and run

```bash
cd Redis-Server
make                      # g++ -std=c++17 -Wall -Wextra -pthread -MMD -MP -O2
./my_redis_server 6379 dump.my_rdb 300
```

The Makefile's source glob (`Makefile:12`) is `$(shell find $(SRC_DIR) -name '*.cpp')`
— **recursive**, and the comment at `:9-11` explains why:

> The previous rule used `$(SRC_DIR)/*.cpp` only, so `src/eviction/*.cpp` was
> never compiled and the shipped binary contained **no eviction code at all**.

Targets: `all` · `my_redis_server` · `test` · `test-policy` · `test-integration`
· `clean` · `rebuild` · `run`. Test binaries exclude `main.o` (`:39`) since they
supply their own entry points.

### Tests

```bash
make test-policy        # build/test_eviction
./build/test_eviction   # 16 tests, ~1069 checks
make test-integration
./build/test_integration   # ~180 checks, incl. real sockets on port 16399
./test_all.sh           # ~40 commands piped into redis-cli
```

**`tests/test_eviction.cpp`** (388 lines) links only `build/eviction/*.o` — no
server, no sockets. It tests the policies as pure objects:

| Test | Line | Asserts |
|---|---|---|
| `testLru` | `:55` | recency ordering; no duplicate list entries |
| `testLfuKeepsFrequencyAcrossReinsert` | `:83` | a redundant insert does not reset the counter |
| `testLfuTieBreaksByRecency` | `:109` | frequency ties resolved by older `lastAccess` |
| `testLfuUnknownAccessIsInserted` | `:119` | |
| `testFifoIgnoresAccess` | `:128` | reads never reorder the queue |
| `testFifoStaysBounded` | `:142` | tombstones are reclaimed |
| `testRandomOnlyReturnsMembers` | `:160` | 200 draws, all resident, >1 distinct |
| **`testAdaptiveAdaptsAndKeepsFloor`** | `:182` | 500 rounds; both weights `> 0`, `≥ 0.05 − 1e-9`, and `\|w_LRU + w_LFU − 1\| < 1e-9` |
| **`testAdaptiveShadowLossDecays`** | `:211` | peak loss `> 0`, then strictly smaller after 50 more rounds |
| `testPolicyReplaceKeepsOrdering` | `:234` | `onReplace` moves neither recency nor frequency |
| `testSyncKeys` | `:251`/`:271` | templated across all 5 policies |
| `testEnginePolicyFactory` | `:279` | case-insensitive; `CACHEUS`→`ADAPTIVE`; a failed switch leaves the policy unchanged |
| `testEngineCapacityAndMetrics` | `:298` | `hitRatePercent() == 66` for 2/3 (integer division) |
| `testEngineSelectionCountersFollowPolicy` | `:326` | |
| `testEngineDetailOnlyForAdaptive` | `:341` | `policyDetail()` is `""` for static policies |
| `testUnlimitedCapacity` | `:355` | `MAXKEYS 0` disables enforcement |

**`tests/test_integration.cpp`** (813 lines), two phases.

*Phase 1 — in-process command handler*:

| Test | Line | Covers |
|---|---|---|
| `testStringCommands` | `:120` | |
| `testKeysPattern` | `:161` | glob matching |
| `testTypeSafety` | `:188` | |
| `testListCommands` | `:214` | |
| `testHashCommands` | `:247` | |
| `testExpiry` | `:274` | TTL semantics |
| `testEvictionEnforced` | `:303` | capacity is actually enforced |
| `testEvictionCountsEveryPolicy` | `:326` | all 5 policies evict |
| `testUnlimitedCapacity` | `:343` | |
| `testPolicySwitchKeepsResidentSet` | `:352` | switching policy does not lose keys |
| `testShrinkingCapacityEvictsImmediately` | `:374` | |
| `testMetricsAndInfo` | `:385` | |
| `testAdaptiveDetail` | `:418` | `INFO` exposes the controller state |
| `testPersistenceRoundTrip` | `:437` | dump → load → identical state |
| `testCorruptSnapshotIsRejected` | `:494` | staging prevents partial loads |
| `testSnapshotPopulatesPolicy` | `:514` | loaded keys are tracked by the policy |

*Phase 2 — real sockets on port 16399*:

- `testWireProtocol` (`:642`) — 9000-byte value, a command split across TCP
  segments, pipelining, inline commands, QUIT
- `testConcurrentClients` (`:710`) — 6 threads × 25 SET/GET = 150 keys

Per `docs/Project/Concept-to-Code Map.md:106-109`, both suites run clean under
**ASan, UBSan, and TSan** with zero findings. That is not a claim you can make
about a project that only has a smoke test.

### UI

```bash
pip install -r requirements-ui.txt
streamlit run ui_app.py
```

### Benchmark

```bash
python Benchmark/generate_stock_workload.py --output stock_market_data.csv
python Benchmark/benchmark_client.py --stock-csv stock_market_data.csv \
    --policy ADAPTIVE --max-keys 25 --operations 300 --concurrency 1 5 \
    --label adaptive
python Benchmark/analyze_results.py Benchmark/results
```

`run_experiments.bat` does the full five-policy sweep in one go.

### Docs validation

```bash
python tools/validate_vault.py
```

---

## 10. Design Decisions and Their Trade-offs

`docs/Project/Architecture.md:87-97` publishes the reasoning. It is unusual and
valuable — most student projects hide this.

| Decision | Reason |
|---|---|
| **One global mutex** | The simplest *correct* option. A deliberate scalability ceiling, not an oversight. |
| **Hash index only** | The workload is entirely point lookups. |
| **Capacity in keys, not bytes** | Simpler. Bytes are computed and reported, just not enforced. |
| **Lazy TTL expiry** | No background scan thread; expiry happens on access and on policy/config changes. |
| **Snapshot, not WAL** | Crash-safe at the file level (temp + rename); loses at most one interval of writes. |
| **Length-prefixed text records** | Binary-safe *and* human-readable. Not slotted pages, not space-reclaimable. |
| **Detached client threads** | Avoids retaining a `std::thread` object per connection. |
| **Joined snapshot thread** | Must not outlive the process. |
| **Logical clocks in policies** | Deterministic benchmarks and reproducible unit tests. |
| **Policies carry no payload** | One implementation serves server, tests, and offline replay. |
| **`onReplace` is a no-op by default** | A value rewrite is neither a read nor a new arrival; it must not perturb recency or frequency. |
| **`registerEvictionDecision` is on the interface** | A policy must be able to learn from the candidate it *didn't* get. This is the hook ADAPTIVE hangs off. |

### 10.6 Legacy name compatibility

`canonicalPolicyName()` (`eviction_engine.cpp:44-50`) accepts `CACHEUS` and maps
it to `ADAPTIVE`. `run_experiments.bat` still passes `--policy CACHEUS` for the
adaptive run. This keeps older scripts working while the project's documentation
is unambiguous that the policy is called `ADAPTIVE` and is not CacheUS.

---

## 11. Bugs Found and Fixed

This project's most distinctive feature is that its own comments record the bugs.
That is a stronger signal of understanding than a clean-looking codebase.

| Bug | Symptom | Fix | Location |
|---|---|---|---|
| Non-recursive `Makefile` glob | `src/eviction/*.cpp` never compiled — **the shipped binary had no eviction code at all** | `$(shell find $(SRC_DIR) -name '*.cpp')` | `Makefile:9-12` |
| RESP header double-strip | `header.substr(1)` dropped the first digit of every count/length — **every `redis-cli` command failed** | removed the redundant strip | `RedisCommandHandler.cpp:98-99` |
| `consumed = 0` on reject | read loop spun forever on a bad buffer | `reject()` sets `consumed = buffer.size()` | `RedisCommandHandler.cpp:69-72` |
| Old parse signature returned only tokens | pipelining impossible, partial commands discarded, >1024-byte commands truncated | added `std::size_t& consumed` | `docs/Project/RESP Protocol.md:46-52` |
| Adaptive losses never decayed | cumulative loss → `exp(−lr·regret)` shrank to a no-op → **weights froze** | `lossDecay_ = 0.90` | `adaptive_policy.cpp:69-73` |
| Adaptive had no weight floor | a dead expert could never recover from a workload shift | `kMinWeight_ = 0.05`, floored before normalize | `adaptive_policy.h:56-58` |
| Erase-while-iterating `expiry_map` | **use-after-free caught by ASan** | collect-then-remove | `RedisDatabase.cpp:148-163` |
| Public method called from a locked one | `configGet()` → `totalKeyBytes()` → **self-deadlock** on a non-recursive mutex | the `...Locked()` naming convention | `docs/OS/Mutual Exclusion.md:80-83` |
| Three parallel value maps | one key could be a string *and* list *and* hash — duplicate `KEYS` output, meaningless capacity | one `Value` with a type tag | `RedisDatabase.h:101-103` |
| `readChunk` over-consumed separators | values starting with a space read as `" user:"` not `"user:1"` — **silent data corruption** | consume exactly one separator char | `RedisDatabase.cpp:851-852` |
| `syncKeys` rescanned per key | bulk reconcile was **quadratic** | build `wanted` set once, two passes | `lru_policy.cpp:27-43` |
| FIFO tombstones unbounded | deque grew forever as keys were individually deleted | `trimFront()` + head/tail compaction | `fifo_policy.cpp:16-19`, `:39-42` |
| `resyncPolicy` didn't sort by insertion time | FIFO resumed in **hash order** after a policy switch | sort ascending first | `RedisDatabase.cpp:90-93` |
| Admin commands missing | benchmark/UI issued `INFO`, `CONFIG SET POLICY`, `CONFIG RESETSTAT`, `EVICTION`, `MAXKEYS` — **runs "succeeded" while measuring nothing** | implemented all of them | `RedisCommandHandler.cpp:461-467` |
| Duplicate policy tree in `eviction_policies/` | two byte-identical copies drifted; **only one was ever compiled** | deleted the duplicate | `eviction_policies/README.md:15-19` |

### Open quirks (documented, not fixed)

These are behavioural oddities that are currently *correct enough* but would
surprise a reader:

1. **`EvictionEngine::onReplace()` does not forward to `policy_->onReplace()`**
   (`eviction_engine.cpp:84-86`). It only increments `totalAccesses`. Today this
   is behaviourally identical because all four concrete policies inherit the
   no-op default — **but `AdaptivePolicy` overrides it** (`adaptive_policy.h:30`,
   `adaptive_policy.cpp:40-45`) and it is therefore never invoked through the
   engine.

2. **`RedisDatabase::keys()` calls `engine.onAccess()` for every matched key**
   (`RedisDatabase.cpp:368`). A `KEYS *` therefore refreshes recency and
   frequency for the **entire** keyspace while not incrementing `totalHits`.

3. **`SET ... PX` rounds up** to whole seconds (`(amount + 999) / 1000`,
   `RedisCommandHandler.cpp:203`).

4. **Hit rate uses truncating integer division** — 2 hits / 1 miss reports `66`,
   not `66.67`.

5. **Build artifacts are tracked.** `my_redis_server`, `build/*.o`, and
   `dump.my_rdb` are in git despite `.gitignore` listing `build/` and `*.my_rdb`
   — they predate the ignore rules.

6. **`docs/README.md` says "31 notes"; there are 32.** `Home.md` and
   `README.md` were added after the count was written.

7. **`Benchmark/results/` and `Benchmark/analysis_output/` do not exist** in this
   checkout — no benchmark has been run. The Benchmarks tab shows instructions
   instead of data.

---

## 12. Quick Reference: Constants and Hyperparameters

| Constant | Value | Location |
|---|---|---|
| Adaptive learning rate | `0.20` | `adaptive_policy.h:25` |
| Adaptive loss decay | `0.90` | `adaptive_policy.h:25` |
| Adaptive weight floor | `0.05` | `adaptive_policy.h:58` |
| Adaptive initial weights | `0.5 / 0.5` | `adaptive_policy.h:60-61` |
| Adaptive shadow-loss increment | `+1.0` | `adaptive_policy.cpp:18`, `:22` |
| Adaptive preferred expert | `weightLRU_ >= weightLFU_` | `adaptive_policy.h:41-43` |
| Default engine policy | `ADAPTIVE` | `eviction_engine.cpp:33-36` |
| Hit-rate formula | `(hits*100)/(hits+misses)`, integer | `eviction_metrics.h:32-36` |
| Server read chunk | `8192` bytes | `RedisServer.cpp:23` |
| Max pending bytes per connection | `16 MB` | `RedisServer.cpp:24` |
| Listen backlog | `128` | `RedisServer.cpp:153` |
| Graceful-shutdown sleep | `100 ms` | `RedisServer.cpp:231` |
| Persistence poll slice | `200 ms` | `RedisServer.cpp:180` |
| Default snapshot interval | `300 s` | `main.cpp:40` |
| Max multibulk elements | `1,048,576` | `RedisCommandHandler.cpp:32` |
| Max bulk length | `512 MB` | `RedisCommandHandler.cpp:33` |
| Max records per key (snapshot) | `10,000,000` | `RedisDatabase.cpp:830` |
| Max record bytes (snapshot) | `512 MB` | `RedisDatabase.cpp:831` |
| Integration test port | `16399` | `test_integration.cpp:767` |
| UI history limit | `120` samples | `ui_app.py:33` |
| Dashboard refresh | `2.0 s` | `ui_app.py:726` |
| Eviction lab refresh | `1.0 s` | `ui_app.py:441` |
| yfinance cache TTL | `600 s` | `ui_app.py:572` |
| Client socket timeout | `3.0 s` | `redis_client.py:49` |
| Benchmark `--hit-target-percent` | `80.0` | `benchmark_client.py:173` |
| Benchmark `--max-keys` (experiments) | `25` | `run_experiments.bat` |
| Benchmark `--operations` (experiments) | `300` | `run_experiments.bat` |
| Benchmark concurrency levels | `1 5` | `run_experiments.bat` |
| P95 percentile method | nearest-rank | `benchmark_client.py:15-18` |
| Workload phase boundaries | `0–299 / 300–699 / 700–999` | `generate_stock_workload.py:26-31` |
| Workload phase weights | `10×` tech, `15×` earnings, `1×` uniform | `generate_stock_workload.py:27-31` |
| Test check counts | 1069 policy / 180 integration | `docs/Project/Concept-to-Code Map.md:106-107` |
| Vault source references | `261` | verified by `tools/validate_vault.py` |

---

## 13. Known Gaps and Future Work

`docs/Project/Gaps and Future Work.md` publishes a prioritised roadmap. The
framing is consistent with the vault's honesty rule: these are findings, not
oversights.

### P1 — high value, small effort

| Item | Why |
|---|---|
| **Write-ahead logging** | Everything is already there: `CacheEntry::dirty`, `logicalClock_` as an LSN, and a staged `load()` to extend. |
| **Ghost cache** | *"The single change that would make ADAPTIVE genuinely adaptive rather than a weighted switch between two static rules."* |
| **Byte-bounded capacity** | `CacheEntry::byteSize` is already computed for every key; only enforcement is missing. |
| **CLOCK / second-chance** | ~40 lines, and the standard comparison point every DBMS course uses. |

### P2 — larger features

B+ tree · `RANGE` + `EXPLAIN` · sharded locking · reader-writer split ·
`SCAN` with cursors · `HELLO`/RESP3 · a real CacheUS implementation

### P3 — completeness

`MULTI`/`EXEC`/`DISCARD` · deadlock detection · slotted pages · `fsync` ·
compression and checksums · incremental save · snapshot format versioning ·
`RLIMIT_AS`

### Explicitly out of scope (`:100-103`)

Replication · clustering · pub/sub · Lua scripting · multi-process operation

---

## 14. How to Read the Results Honestly

`Benchmark/README.md:103-112` and `docs/Project/Benchmarking.md:106-129` both
list caveats. They belong in any presentation of these numbers:

1. **The cache stays warm across concurrency levels.** The `c=5` pass is not
   flushed, so it starts from whatever `c=1` left behind. Use `--no-flush` for a
   clean comparison.
2. **Latency partly measures policy cost, not just server speed.** LFU and FIFO
   select victims in **O(n)**; LRU and RANDOM in **O(1)**. A latency gap between
   policies is partly an algorithmic-complexity gap.
3. **`approx_ops_per_second` is derived** from the CSV timestamp span. The
   authoritative figure is `throughput_ops_sec` in the JSON.
4. **Effect sizes are small** at 300 operations over 25 keys — often within
   noise. Raise `--operations` before claiming a difference.
5. **`--hit-target-percent 80` is a target, not a result.** Reporting it as the
   observed hit rate would be misleading; use `observed_hit_rate_percent` from
   `analyze_results.py`.
6. **RANDOM is the null model.** A policy that does not beat random is not
   learning anything about the workload.

---

## 15. One-Page Summary

```
                        ┌──────────────────────────────────┐
  redis-cli / UI /      │        RedisServer               │
  benchmark_client ────►│  accept loop · detached threads  │
  (RESP over TCP)       │  SIGINT/SIGTERM → shutdown()     │
                        └──────────────┬───────────────────┘
                                       │
                        ┌──────────────▼───────────────────┐
                        │      RedisCommandHandler        │
                        │  parseRespCommand (consumed)     │
                        │  dispatch: SET/GET/LIST/HASH/…  │
                        └──────────────┬───────────────────┘
                                       │
                        ┌──────────────▼───────────────────┐
                        │          RedisDatabase           │
                        │  store · frames · expiry_map     │
                        │  db_mutex (global)               │
                        │  enforceCapacityLocked()  ◄── the │
                        │                                 │  buffer
                        └──────────────┬───────────────────┘  manager
                                       │
                        ┌──────────────▼───────────────────┐
                        │         EvictionEngine           │
                        │  metrics · factory · resync      │
                        └──────────────┬───────────────────┘
                                       │
        ┌──────────┬───────────┬────────┴────────┬──────────────┐
        ▼          ▼           ▼                 ▼              ▼
      LRU        LFU         FIFO            RANDOM        ADAPTIVE
      O(1)       O(n)        O(n)              O(1)     ┌────┴────┐
     list+map   freq map    deque+tomb.      vector+    LRU expert
                          (ignores reads)    mt19937    LFU expert
                                                        2 weights
                                                        shadow loss
                                                        exp(−0.2·regret)
```

**The one thing to remember:** every design decision in this project is
traceable to a DBMS buffer-pool concept, and every one of them — including the
things that are *absent* — is written down and, where it points at code, verified
by `tools/validate_vault.py`.