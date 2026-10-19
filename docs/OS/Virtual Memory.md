---
tags: [os, virtual-memory, memory, heap, project/adaptive-cache-redis]
status: partial
---

# Virtual Memory

## Definition

Every process gets its own **virtual address space** — a private, contiguous
range of addresses that the kernel maps onto physical memory and disk. Programs
compile and link against addresses that need not correspond to any particular
RAM location.

Consequences:
- **Isolation** — one process cannot read another's memory.
- **Abstraction** — a program sees a linear address space; the kernel decides
  the physical layout.
- **Overcommit** — more virtual memory may be reserved than is physically
  present; pages materialise on first touch.
- **Protection** — read/write/execute permissions per page.

---

## In this project

### The store lives on the heap

The entire dataset is an ordinary heap-allocated `std::unordered_map`:

`src/RedisDatabase.cpp`
```cpp
std::unordered_map<std::string, Value> store;
```

There is nothing exotic. The server's design rests on the fact that this
structure fits in RAM and is never paged to disk by the application — which is
also exactly the assumption a buffer pool makes.

### Memory growth is unbounded

> [!warning] Not implemented
> Nothing limits resident memory by **bytes**. Capacity is measured in **keys**
> (`MAXKEYS`), so a cache holding 10,000 small values and one holding 10,000
> 1 MB values are treated identically.
>
> The byte accounting exists but is advisory only — it is reported by
> `INFO bytes_used` and `MEMORY USAGE <key>`, and is never enforced.

`src/RedisDatabase.cpp` — `valueBytesLocked()`
```cpp
std::size_t RedisDatabase::valueBytesLocked(const Value& value) const {
    std::size_t total = 0;
    switch (value.type) {
        case Value::Type::String:
            total = value.str.size();
            break;
        case Value::Type::List:
            total = sizeof(std::string) * value.list.capacity();
            for (const auto& item : value.list) total += sizeof(std::string) + item.size();
            break;
        ...
```

### Per-key frame metadata

Each key carries a `CacheEntry` alongside its value:

`include/eviction/cache_entry.h`
```cpp
struct CacheEntry {
    std::string key;
    std::uint64_t insertionTime{0};
    std::uint64_t lastAccessTime{0};
    std::uint64_t accessCount{0};
    std::size_t byteSize{0};
    bool dirty{false};
};
```

> [!info] Logical clocks, not wall-clock timestamps
> These are **counters, not times**. `lastAccessTime` holds a tick from an
> internal `logicalClock_` incremented on each access. Using logical time makes
> unit tests and replayed benchmarks deterministic — two runs of the same
> workload produce identical policy decisions regardless of machine speed.

---

## Not implemented

> [!warning] Absent entirely
> - No `mmap`, no memory-mapped files, no `madvise`.
> - No huge pages or allocator tuning.
> - No memory limit (`setrlimit(RLIMIT_AS)`), so a hostile client can
>   exhaust RAM by writing large values until eviction catches up.
> - No swap awareness: the OS may page the heap out, turning every "memory hit"
>   into a disk fault. The server cannot detect or count this.

> [!tip] Honest framing for a viva
> "The dataset is a single heap-allocated hash map. Because the server does not
> control total resident size, the OS is free to page it — which would silently
> convert cache hits into page faults. Measuring that would require reading
> `/proc/self/stat` minor-fault counters, which is a natural extension."

---

## Related

- [[DBMS/Buffer Pool]] — the DBMS term for the same idea
- [[DBMS/File Organization]] — moving data between memory and disk
- [[OS/File Systems and VFS]] — where snapshots land
- [[Project/Gaps and Future Work]] — byte-bounded capacity