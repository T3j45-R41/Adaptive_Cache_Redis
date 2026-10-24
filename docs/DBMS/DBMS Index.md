---
tags: [moc, dbms, project/adaptive-cache-redis]
status: index
---

# DBMS Index

Database-management concepts used by **Adaptive Cache Redis**, each linked to the
code that exercises it.

> [!important] The central reframe
> The project's cache **is a buffer pool**. A buffer pool holds data in memory
> to avoid disk reads; when it is full it must discard something. Every
> eviction policy in this project is a textbook **page-replacement** policy.
> The difference is that the "disk" is the network and the backing store, and
> the pages are network-served objects rather than disk blocks.
>
> Once you say this out loud, the rest of the DBMS content stops being bolted
> on and becomes structural.

---

## Storage management — the core of this project

1. [[DBMS/Buffer Pool]] — the memory-resident store, page table, frame metadata
2. [[DBMS/Buffer Replacement Policies]] — LRU, LFU, FIFO, random, and their cost
3. [[DBMS/Adaptive Replacement]] — the two-expert controller, and why it is not CacheUS

## Physical storage

4. [[DBMS/File Organization]] — record layout, slotted pages, free-space management
5. [[DBMS/Recovery and Logging]] — write-ahead logging, checkpoints, why snapshots are weak

## Access paths

6. [[DBMS/Indexing]] — hash indexes, B+ trees, and what this project has
7. [[DBMS/Query Processing]] — parsers, optimisers, cost-based plan selection

## Logical design

8. [[DBMS/Transactions and ACID]] — atomicity, durability, isolation, consistency
9. [[DBMS/Concurrency Control]] — locking, isolation levels, deadlocks, MVCC
10. [[DBMS/Normalization]] — functional dependencies, 1NF–BCNF, the stock schema

---

## Honest scope

> [!warning] What this project does NOT implement
> | Concept | Status |
> | --- | --- |
> | Buffer pool + page replacement | ✅ Implemented — the core of the project |
> | Hash index | ✅ Implemented, as `unordered_map` |
> | B+ tree index | ❌ Not implemented |
> | Secondary indexes / range scans | ❌ Not implemented |
> | Query planner / optimiser | ❌ Not implemented |
> | Transactions (`MULTI`/`EXEC`) | ❌ Not implemented |
> | Two-phase locking, deadlocks | ❌ Not implemented |
> | Write-ahead log, ARIES | ❌ Not implemented |
> | Slotted pages | ❌ Not implemented (plain length-prefixed lines) |
> | Normalization | 📄 Documented only — see [[DBMS/Normalization]] |
>
> Each note states precisely which of these it covers. The project's honest
> position is that it is a **buffer-management study**, not a general-purpose
> database — and that is a legitimate, well-scoped DBMS contribution.

---

## Related

- [[OS Index]] — the operating-systems side
- [[Project/Gaps and Future Work]] — the full extension plan