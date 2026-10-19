---
tags: [os, clocks, time, monotonic, ttl, project/adaptive-cache-redis]
status: implemented
---

# Clocks and Time

## Definition

C++ exposes several clocks, and choosing the wrong one produces subtle bugs:

| Clock | Meaning | Adjustable? |
| --- | --- | --- |
| `system_clock` | Wall-clock time since the Unix epoch | **Yes** — NTP, DST, manual `settimeofday` |
| `steady_clock` | Monotonically increasing elapsed time | **Never** |
| `high_resolution_clock` | Alias; usually an alias for `steady_clock` | — |

> [!important] The rule
> Use `steady_clock` for **durations and intervals** — anything measured by
> subtracting two timestamps. Use `system_clock` only when you need an actual
> calendar date.

---

## In this project

### TTLs require monotonic time

`include/RedisDatabase.h`
```cpp
using Clock = std::chrono::steady_clock;
std::unordered_map<std::string, Clock::time_point> expiry_map;
```

`src/RedisDatabase.cpp`
```cpp
expiry_map[key] = Clock::now() + std::chrono::seconds(seconds);
...
if (now > it->second) expired.push_back(entry.first);
```

> [!warning] Why this is not pedantry
> With `system_clock`, an NTP correction that steps the clock **backwards** by
> an hour would make every TTL an hour longer, so keys would outlive their
> intended lifetime and the cache would serve stale data. Stepping it **forwards**
> would expire everything at once — a thundering herd against the backing store.
>
> `steady_clock` is backed by `CLOCK_MONOTONIC`, which the kernel guarantees
> never runs backwards. Timeouts, TTLs, and rate limiters must all use it.

### The snapshot interval

`src/RedisServer.cpp:182`–`src/RedisServer.cpp:189`
```cpp
const auto interval = std::chrono::seconds(persistIntervalSeconds);
auto elapsed = std::chrono::steady_clock::duration::zero();

while (persistRunning.load()) {
    std::this_thread::sleep_for(slice);
    elapsed += slice;
    if (elapsed < interval) continue;
    elapsed = std::chrono::steady_clock::duration::zero();
```

Accumulating a `duration` rather than comparing wall-clock timestamps keeps the
loop immune to clock adjustments over a long-running process.

### Logical clocks inside the cache

Separately from real time, the policies use **logical counters**:

`include/eviction/lfu_policy.h:32`
```cpp
std::uint64_t logicalClock_{0};
```

`include/eviction/cache_entry.h`
```cpp
std::uint64_t insertionTime{0};
std::uint64_t lastAccessTime{0};
```

> [!tip] Why logical time at all?
> Because the whole project is a *comparative* study. If policy decisions
> depended on real elapsed time, two runs of the same workload on a loaded vs.
> an idle machine would produce different eviction choices, and the comparison
> would be meaningless. With logical clocks the workload is fully deterministic:
> same operations in, same evictions out.

### TTL semantics as implemented

| Value | Meaning |
| --- | --- |
| `>= 0` | Seconds remaining |
| `-1` | Key exists, no expiry set |
| `-2` | **No such key** |

Matching Redis exactly, so the Streamlit panel and `redis-cli` both work.

### Expiry is lazy, not active

> [!abstract] Partial
> `purgeExpiredLocked()` runs at the start of most operations and reaps due
> keys. Real Redis additionally runs an *active* expiry cycle that samples keys
> even when nothing is touching them.
>
> Consequence here: a key with a TTL that is never read again lingers in memory
> until something else triggers a purge. It cannot be served — every read purges
> first — but it does occupy capacity and can be counted as a resident key.

---

## Related

- [[OS/Atomics and Memory Ordering]] — `steady_clock` for defined behaviour
- [[DBMS/Buffer Pool]] — how expiration interacts with eviction
- [[Project/Benchmarking]] — why determinism matters for the results