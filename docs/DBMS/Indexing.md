---
tags: [dbms, indexing, hash-index, b-plus-tree, access-path, project/adaptive-cache-redis]
status: partial
---

# Indexing

## Definition

An **index** is a data structure that accelerates lookups by maintaining a
mapping from search key to the location of the record.

### Hash index

Bucket records by `hash(key) % B`. Exact-match lookups in O(1) average.
**Cannot** answer ranges, prefixes, or ordering. Hash functions with many
collisions degrade badly unless buckets are chained or rehashed.

### B+ tree

The workhorse of relational indexes. Properties that matter:

- **All data in leaves**, so a tree of height *h* needs *h + 1* I/Os for a
  point lookup — height 3 means 3 page reads regardless of table size.
- **Linked leaves** in key order, so a range scan is a sequential walk.
- **High fanout** (hundreds of keys per node), so the tree stays shallow.
- Keys are **ordered**, which is what makes range queries possible at all.

This is why database indexes are B+ trees and not binary search trees: I/O count,
not comparison count, is the cost being minimised.

---

## In this project

### The hash index

`src/RedisDatabase.cpp`
```cpp
std::unordered_map<std::string, Value> store;
```

This is a chained hash index with O(1) average lookup. Every `GET`, `SET`,
`DEL`, `TTL` and `MEMORY USAGE` goes through it.

> [!info] The cost-model tie-in
> The cost of a point lookup is one in-memory hash probe, not one page read.
> That is exactly the "index vs. sequential scan" trade-off — the index wins
> because the data is already in the buffer pool. See [[DBMS/Buffer Pool]].

### The secondary structure

`src/RedisDatabase.cpp`
```cpp
std::unordered_map<std::string, CacheEntry> frames;
std::unordered_map<std::string, Clock::time_point> expiry_map;
```

Both are keyed by the same key, so lookups are parallel probes rather than
joins. The frame map is the analogue of a data directory: a small, fixed-size
structure pointing at the larger payload — the same idea as a B+ tree's
separating the key from the record.

### The access path

```
key ──▶ hash probe ──▶ Value + CacheEntry ──▶ payload
         O(1) avg        O(1)                  variable size
```

---

## Not implemented

> [!warning] Absent — and this is a real gap
> | Structure | Status |
> | --- | --- |
> | Hash index | ✅ Implicit, via `unordered_map` |
> | B+ tree | ❌ |
> | Secondary indexes | ❌ |
> | Range scans / range predicates | ❌ |
> | Multi-column / composite keys | ❌ |
> | Unique indexes, covering indexes | ❌ |
> | Prefix indexes | ❌ |
> | Index selectivity and optimiser choice | ❌ |
>
> Consequently there is exactly **one** access path. Every question the
> benchmark asks is a point lookup.

### `KEYS` is a scan, not an index

`src/RedisDatabase.cpp:172`
```cpp
bool RedisDatabase::globMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
```

A full hash-table walk with glob matching per key — O(n) always, with no index
to narrow it. This is a genuine sequential scan, and it is why `KEYS *` is
excluded from measured benchmark operations.

> [!bug] A bug this fixed
> `handleKeys` originally **ignored its pattern argument entirely** and returned
> every key in the store. `KEYS stock:*` returned unrelated keys. Covered by
> `testKeysPattern`.

---

## The obvious extension

> [!tip] Why a B+ tree would fit this project
> The natural key is the stock-quote scheme already in use:
>
> | Key | Range query it would enable |
> | --- | --- |
> | `yf:AAPL:close:*` | all daily closes for AAPL, in date order |
> | `yf:*:close:2026-01-*` | one trading day across all tickers |
>
> With a B+ tree over the composite `(ticker, date)` key, the eviction lab could
> evict a whole time range, and a `ZRANGE`-style command could fetch the last *n*
> closes without scanning. That would turn the project from a buffer-pool study
> into a buffer-pool **plus index** study.
>
> See [[Project/Gaps and Future Work]].

---

## Related

- [[DBMS/Query Processing]] — choosing between access paths
- [[DBMS/File Organization]] — where the indexed records live
- [[DBMS/Buffer Pool]] — why the index fits in memory here
- [[DBMS/Normalization]] — deciding which columns deserve an index
- [[Project/Gaps and Future Work]] — the B+ tree extension