---
tags: [dbms, concurrency-control, locking, deadlocks, isolation, mvcc, project/adaptive-cache-redis]
status: partial
---

# Concurrency Control

## Definition

Mechanisms ensuring concurrent transactions produce a correct, serialisable
result.

### Lock-based

**Two-phase locking (2PL)**: a transaction may acquire locks, but once it
releases one it may not acquire any more. *Growing* phase then *shrinking*
phase. 2PL prevents cascading aborts but can deadlock.

Lock compatibility:

| | S | X |
| --- | --- | --- |
| **S** | ✅ | ❌ |
| **X** | ❌ | ❌ |

**Deadlock avoidance**: wound-wait, wait-die, timestamps. **Deadlock
*detection***: a wait-for graph with cycle detection, then victim selection.

### Lock-free

- **MVCC** — multiple versions per row; readers take a snapshot and never
  block. Writes do not block reads either; conflicts are resolved at commit.
- **Timestamp ordering** — each transaction gets a timestamp; older commits wait
  or abort.
- **Optimistic** — validate at commit, abort on conflict.

---

## In this project

> [!abstract] Partial: coarse-grained exclusive locking only
> The project has **2PL with one exclusive lock held for the whole operation**.
> Because the lock is taken and released entirely within a single method call,
> the growing and shrinking phases are instantaneous and no deadlock is
> possible.

`include/RedisDatabase.h`
```cpp
mutable std::mutex db_mutex;
```

`src/RedisDatabase.cpp:324`
```cpp
OpStatus RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);   // growing phase (instant)
    ...
    return OpStatus:: Ok;                         // shrinking phase (destructor)
}
```

### Why there is no deadlock

> [!important] The structural argument
> Deadlock needs a cycle in a wait-for graph, which needs at least two locks.
> There is exactly one lock, and a thread never blocks on anything while
> holding it. The wait-for graph has no edges. Deadlock is impossible by
> construction, not by luck.
>
> This is worth stating explicitly — "we don't handle deadlocks" is a much
> weaker answer than "deadlock cannot arise here, and here is why".

### Cost

| Property | Value |
| --- | --- |
| Lock granularity | One global lock |
| Reader/writer | Exclusive — no shared locks |
| Concurrency | Effectively 1 |
| Deadlock | Impossible |
| Starvation | Possible in principle; no fairness mechanism |

> [!warning] The honest limitation
> A global mutex is the dominant performance ceiling of this project. It is a
> deliberate simplification. It does **not** weaken correctness — serialisation
> is always safe — but it means throughput does not scale with cores.

---

## Not implemented

> [!warning] Absent
> - **Shared (S) locks / reader-writer locks** — reads block reads.
> - **Multi-granularity / intention locks** for nested structures.
> - **Deadlock detection** (wait-for graph) — unnecessary today, required the
>   moment a second lock exists.
> - **MVCC or snapshot isolation** — no versioning, so no non-blocking reads.
> - **Lock timeout / queueing fairness.**
> - **Lock granularity by key or shard.**

---

## The natural next step

> [!tip] Sharded locking
> Striping the keyspace across N mutexes, with the shard index computed from
> the key's hash, would give real concurrency while keeping deadlock impossible
> if a thread never holds two shard locks at once. That is a small, safe change
> with a large measurable effect — and it maps directly onto the "buffer pool
> concurrency" question.
>
> Second step: upgrade to a reader-writer lock so `KEYS *` and `HGETALL` stop
> blocking writers.

---

## Related

- [[OS/Mutual Exclusion]] — the actual mutex mechanics and the re-entrancy trap
- [[DBMS/Transactions and ACID]] — what isolation is protecting
- [[OS/Atomics and Memory Ordering]] — the lock-free flags around the lock
- [[Project/Gaps and Future Work]] — sharded locking