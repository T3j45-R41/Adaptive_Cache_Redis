---
tags: [dbms, storage-manager, buffer-pool, page-replacement, project/adaptive-cache-redis]
status: implemented
---

# Buffer Pool

## Definition

The **storage manager** mediates between the database and disk. Its central
component is the **buffer pool**: a region of memory holding database pages
that have been read from disk or dirtied by recent writes, so that repeated
access does not require I/O.

Two structures are essential:

- **Page table** — maps a page identifier to the buffer-pool frame holding it.
- **Buffer frames** — the actual slots. Each frame carries metadata: a **dirty
  bit** (modified since last write-back), a **pin count** (in use by an active
  operation, so it must not be chosen for replacement), and, in real systems,
  an LSN.

When the pool is full, the **buffer manager** selects a victim frame, writes it
back if dirty, and loads the requested page into it.

> [!important] The framing that makes this project make sense
> This project's cache is a buffer pool. The "disk" is the network and the
> backing store; the "pages" are cached network-served objects. Eviction *is*
> page replacement, and the five policies in `eviction_policies/` are the
> textbook replacement policies.
>
> This is not a metaphor — it is the same problem with a different backing
> store. Once stated, the project's DBMS content is structural, not bolted on.

---

## In this project

### The pool

`src/RedisDatabase.cpp`
```cpp
std::unordered_map<std::string, Value> store;    // the resident pages
std::unordered_map<std::string, CacheEntry> frames; // the frame table
std::unordered_map<std::string, Clock::time_point> expiry_map;
EvictionEngine engine;
```

### The frame table

`include/eviction/cache_entry.h`
```cpp
struct CacheEntry {
    std::string key;

    // Logical clock ticks, not wall-clock time, so replayed benchmarks and unit
    // tests are deterministic.
    std::uint64_t insertionTime{0};
    std::uint64_t lastAccessTime{0};
    std::uint64_t accessCount{0};

    // Approximate resident footprint: key + payload + per-entry overhead.
    std::size_t byteSize{0};

    // Modified since the last snapshot write-back. An evicted dirty frame costs
    // one write I/O; a clean one costs nothing.
    bool dirty{false};
};
```

| DBMS field | Here | Purpose |
| --- | --- | --- |
| Page number | `key` | Identity |
| Dirty bit | `dirty` | Decides whether eviction costs a write |
| Pin count | *absent* | Every access is instantaneous, so no pinning is needed |
| LSN | *absent* | No logging — see [[DBMS/Recovery and Logging]] |
| Frame size | `byteSize` | Advisory only; capacity is measured in keys |

### Buffer manager

`src/RedisDatabase.cpp:110`–`src/RedisDatabase.cpp:129`
```cpp
void RedisDatabase::enforceCapacityLocked() {
    if (!engine.capacityLimited()) return;

    while (store.size() > engine.maxKeys()) {
        const std::string victim = engine.selectVictim();
        if (victim.empty()) break; // policy sees no candidate; nothing to do

        const bool dirty = frames.count(victim) ? frames[victim].dirty : false;

        // Snapshot the alternatives while the key is still resident, otherwise
        // the adaptive controller has nothing left to shadow.
        engine.registerEvictionDecisionFeedback(victim);

        removeKeyLocked(victim, RemovalReason::Evicted);
        engine.recordEviction(dirty);
        engine.onCapacityEnforced();
    }
}
```

That is the whole buffer manager: pick a victim, write back if dirty, free the
frame. The write-back is elided because this server's only backing store is a
periodic snapshot rather than an on-demand page write.

### Access accounting

`src/RedisDatabase.cpp:82`–`src/RedisDatabase.cpp:88`
```cpp
void RedisDatabase::registerAccessLocked(const std::string& key) {
    auto it = frames.find(key);
    if (it == frames.end()) return;
    it->second.lastAccessTime = logicalClock_++;
    ++it->second.accessCount;
    engine.onAccess(key);
}
```

### The metric that matters

> [!tip] `write_backs` is the DBMS metric
> `INFO` reports `write_backs` — evictions of frames that were dirty at the
> time. That is the I/O a buffer pool would genuinely incur. Total evictions
> alone is a policy-behaviour metric; write-backs is a cost metric.
>
> Clean frames are cleared after each snapshot:
> ```cpp
> for (auto& entry : frames) entry.second.dirty = false;
> ```

---

## Differences from a real DBMS buffer pool

> [!warning] Not implemented
> - **No fixed-size frames or pages.** Values are variable-length; `byteSize`
>   is reported but never used to enforce a byte budget.
> - **No pin counts.** Correct here only because no operation is long-running.
> - **No page replacement on *read*.** A real pool replaces a frame when a
>   requested page is not resident. This server only evicts on write.
> - **No write-back on eviction.** Dirty frames are not persisted at eviction
>   time; the next snapshot captures them.
> - **No page-level locking.** One global mutex.
> - **No clock/second-chance hand** — a cheap, textbook alternative that would
>   be a natural addition.

---

## Related

- [[DBMS/Buffer Replacement Policies]] — how the victim is chosen
- [[DBMS/Adaptive Replacement]] — the project's research contribution
- [[DBMS/File Organization]] — the other half of the storage manager
- [[OS/Virtual Memory]] — what backs the "memory" here
- [[OS/Mutual Exclusion]] — protecting the pool