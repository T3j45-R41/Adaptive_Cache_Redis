---
tags: [project/adaptive-cache-redis, scope, roadmap, future-work, project/adaptive-cache-redis]
status: reference
---

# Gaps and Future Work

An honest inventory of what the project does **not** do, ordered by value per
unit of effort. Use this to answer "what would you do next?" — and to avoid
overclaiming.

---

## The scope statement

> [!important] What this project is
> A **buffer-management study**: a Redis-compatible server whose in-memory store
> is treated as a buffer pool, with five replacement policies compared under a
> shifting workload.
>
> It is not a general-purpose database, and it does not need to be. A
> well-scoped contribution to buffer management is a stronger project than a
> shallow attempt at everything.

---

## Priority 1 — highest value per effort

### 1.1 Write-ahead logging

> [!tip] Why first
> Demonstrates **three** DBMS topics at once — atomicity (undo), durability
> (redo plus the write-ahead rule), and recovery (ARIES analysis) — from
> infrastructure that already exists.

Available hooks:
- `CacheEntry::dirty` already marks modified frames and is cleared at checkpoints.
- `logicalClock_` already supplies an LSN-shaped counter.
- `load()` already stages-then-commits.

Work: append `<LSN, key, old, new>` per mutation; `fsync` before replying;
redo on startup; stop rewriting the whole dataset each time.

**Demo:** `kill -9` mid-run, restart, show nothing committed was lost.

### 1.2 A ghost cache

The single change that would make `ADAPTIVE` genuinely adaptive rather than a
weighted switch between two static rules.

Work: on eviction, record the key's metadata — reuse distance, size, frequency —
in a bounded ghost list. When a later access hits a ghosted key, the controller
learns that the eviction was wrong instead of merely counting a miss.

Small change, disproportionate benefit. See [[DBMS/Adaptive Replacement]].

### 1.3 Byte-bounded capacity

`MAXKEYS` counts keys, so a cache of 1 MB values and a cache of 10-byte values
are treated identically. `CacheEntry::byteSize` is already computed and
reported — only the enforcement is missing.

### 1.4 CLOCK / second-chance

The cheapest textbook approximation to LRU, roughly 40 lines, and the standard
comparison point for any adaptive-policy study. It also exposes why
`RandomPolicy`'s O(1) draw is not the only cheap option.

---

## Priority 2 — meaningful additions

| Addition | Demonstrates |
| --- | --- |
| **B+ tree over `(ticker, date)`** | Ordered access path, enabling range queries over quote history |
| **`RANGE` + `EXPLAIN`** | Query planning: choosing between access paths using estimated cost |
| **Sharded locking** | Real concurrency without deadlock; the global mutex is the current ceiling |
| **Reader–writer lock** | `KEYS *` and `HGETALL` currently block writers |
| **`SCAN` with a cursor** | Incremental iteration instead of a full scan; also unblocks standard GUIs |
| **`HELLO` / RESP3** | Would let off-the-shelf Redis GUIs connect |
| **Real CacheUS** | Window, segmentation, ML cost model, adaptive sampling |

---

## Priority 3 — completeness

- `MULTI` / `EXEC` / `DISCARD` — transactions
- Shared/exclusive locks, wait-for graph deadlock detection
- Slotted pages, variable-length records, in-place update
- `fsync` on snapshot close
- Snapshot compression and a checksum
- Incremental save rather than full rewrite
- File format versioning with a magic number
- `RLIMIT_AS` or a byte budget, so a hostile client cannot exhaust RAM

---

## Deliberately out of scope

> [!note] Not planned
> Replication, clustering, pub/sub, Lua scripting, secondary storage beyond a
> single snapshot file, and multi-process architecture. Each would dilute the
> buffer-management focus that makes this project coherent.

---

## Known weaknesses to state before being asked

| Weakness | Honest framing |
| --- | --- |
| Global mutex | "A deliberate simplification; it serialises correctly but does not scale. Sharding by key hash is the fix." |
| No WAL | "Durability is deferred to the next snapshot — up to 300 s of loss. A WAL is the first thing I would build." |
| LFU/FIFO victim selection is O(n) | "The policies are compared on cache quality, not selection cost; at these key counts it is negligible." |
| Keyspace-only capacity | "Byte accounting is computed and reported but not enforced." |
| Lazy expiry only | "Expired keys are reaped on access, so they are never served, but they occupy capacity until something triggers a purge." |
| Benchmark effect sizes are small | "300 operations against a 25-key cache; re-run with more operations before drawing conclusions." |
| `ADAPTIVE` is not CacheUS | See [[DBMS/Adaptive Replacement]] — the feature-by-feature comparison is the answer. |

---

## Related

- [[Project/Concept-to-Code Map]] — every ❌ in one table
- [[DBMS Index]] — the per-concept scope statements
- [[Project/Benchmarking]] — how to make the experiment stronger