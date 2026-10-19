---
tags: [dbms, buffer-pool, page-replacement, lru, lfu, fifo, project/adaptive-cache-redis]
status: implemented
---

# Buffer Replacement Policies

## Definition

When the buffer pool is full, a **replacement policy** selects a victim frame.
The classic set, straight from every DBMS textbook:

| Policy | Rule | Strength | Weakness |
| --- | --- | --- | --- |
| **FIFO** | Oldest arrival | Ignores access; O(1) | Evicts hot keys |
| **LRU** | Least recently used | Uses recency; O(1) | Ignores frequency — a scan evicts everything |
| **LFU** | Least frequently used | Uses frequency | Slow to adapt; "aging" problem |
| **Random** | Uniform draw | No assumptions | Worst case, but a useful baseline |
| **MRU** | Most recently used | Good for scans | Terrible normally |
| **Clock / 2nd chance** | Circular sweep + reference bit | Approximates LRU at O(1) | Coarse |

---

## In this project

All five are implemented behind one interface and exercised by the server.

### The interface

`include/eviction/eviction_policy.h`
```cpp
class EvictionPolicy {
public:
    virtual void onInsert(const std::string& key) = 0;
    virtual void onAccess(const std::string& key) = 0;
    virtual void onRemove(const std::string& key) = 0;

    // Rewriting an existing key's value is not a read and not a re-insertion:
    // it must not reset recency or frequency.
    virtual void onReplace(const std::string& /*key*/) {}

    virtual std::string selectVictim() const = 0;

    // Called by the owner *after* a victim has actually been removed...
    virtual void registerEvictionDecision(const std::string& /*selectedVictim*/) {}

    virtual void syncKeys(const std::vector<std::string>& keys) = 0;
    virtual std::string name() const = 0;
};
```

> [!info] The layering that makes this testable
> A policy never sees values, capacity, or a clock — only keys. That is what
> lets the same objects serve the server, the unit tests, and offline replay
> with no changes.

### LRU — O(1) doubly linked list

`include/eviction/lru_policy.h:21`–`include/eviction/lru_policy.h:22`
```cpp
std::list<std::string> order_; // front = MRU, back = LRU
std::unordered_map<std::string, std::list<std::string>::iterator> pos_;
```

`src/eviction/lru_policy.cpp`
```cpp
void LRUPolicy::touch(const std::string& key) {
    auto it = pos_.find(key);
    if (it != pos_.end()) {
        order_.erase(it->second);
        pos_.erase(it);
    }
    order_.push_front(key);
    pos_[key] = order_.begin();
}
```

The iterator map is what makes the splice O(1) instead of O(n). Standard
technique, worth being able to explain from memory.

### LFU — counter table with a recency tiebreak

`include/eviction/lfu_policy.h:26`–`include/eviction/lfu_policy.h:32`
```cpp
struct Meta {
    std::uint64_t frequency{0};
    std::uint64_t lastAccess{0};
};
std::unordered_map<std::string, Meta> meta_;
std::uint64_t logicalClock_{0};
```

Ties on frequency are broken by oldest access, so LFU degenerates gracefully to
FIFO among equally-cold keys.

> [!warning] Two real bugs, both fixed and regression-tested
> **1. Frequency reset on re-insert.** The original did `meta_[key] = Meta{1, clock}`
> unconditionally, so any redundant `SET` reset a hot key's counter and
> promoted colder keys above it. Now:
> ```cpp
> void LFUPolicy::onInsert(const std::string& key) {
>     // A re-insertion of a key that is already tracked must not reset its
>     // counter -- that would let a hot key be demoted by a redundant write.
>     if (meta_.find(key) != meta_.end()) return;
>     ++logicalClock_;
>     meta_[key] = Meta{1, logicalClock_};
> }
> ```
> **2. `onReplace` is a no-op.** Rewriting a value must move neither recency nor
> frequency.

### FIFO — deque with tombstone skipping

`include/eviction/fifo_policy.h:19`–`include/eviction/fifo_policy.h:20`
```cpp
std::deque<std::string> queue_;
std::unordered_set<std::string> present_;
```

> [!warning] Unbounded growth, fixed
> The original `onRemove` erased from `present_` but left the stale entry in
> `queue_`. Over a long benchmark the deque grew without limit even though the
> resident set stayed small. Removal now compacts from the front
> (`src/eviction/fifo_policy.cpp:16`):
> ```cpp
> void FIFOPolicy::trimFront() {
>     while (!queue_.empty() && present_.find(queue_.front()) == present_.end())
>         queue_.pop_front();
> }
> ```
> `syncKeys` additionally compacts the tail.

### Random — the null model

`include/eviction/random_policy.h:25`–`include/eviction/random_policy.h:26`
```cpp
std::vector<std::string> keys_;
std::unordered_set<std::string> members_;
```

Included deliberately: a policy that cannot beat random under a given workload
is not learning anything about it.

---

## Cost asymmetry — report this honestly

> [!important] The policies are not equally fast
> | Policy | Victim selection |
> | --- | --- |
> | LRU | O(1) — one splice |
> | Random | O(1) |
> | LFU | **O(n) scan** over the counter table |
> | FIFO | **O(n) scan** past tombstones |
>
> At cache sizes of hundreds of keys this is irrelevant, and the benchmark runs
> at 25–1000 keys. But a latency comparison partly measures *selection cost*, not
> *cache quality*. Hit rate and evictions are the quality metrics; latency is
> not.

`LRUPolicy::syncKeys` was also quadratic — it rescanned the whole key list for
every tracked key — and is now a single `unordered_set` lookup per key.

---

## Not implemented

> [!warning] Absent
> - **CLOCK / second-chance** — the cheapest textbook approximation to LRU, and
>   the most obvious next policy to add.
> - **MRU, 2Q, SLRU, ARC** — windowed variants.
> - **LFU aging / logarithmic increment** — real LFU implementations decay
>   counters so the policy can forget a key that *was* hot.

---

## Related

- [[DBMS/Buffer Pool]] — what is being replaced
- [[DBMS/Adaptive Replacement]] — this project's contribution
- [[Project/Eviction Engine]] — how the policy is selected and wired
- [[Project/Benchmarking]] — measuring these policies
- [[DBMS/Query Processing]] — the cost model these decisions feed into