---
tags: [moc, project/adaptive-cache-redis]
status: index
---

# Project Index

Notes describing this specific codebase: how it is put together, how its
unusual parts work, and what it deliberately leaves out.

---

## Architecture

1. [[Project/Architecture]] — the three classes, the request path, the storage layout
2. [[Project/Concept-to-Code Map]] — every concept → `file:line`, in one table

## Implementation deep dives

3. [[Project/Eviction Engine]] — policy interface, the engine, per-policy data structures
4. [[Project/RESP Protocol]] — multibulk and inline parsing, pipelining, consumed-byte framing
5. [[Project/Persistence Format]] — the snapshot file format, and why it is length-prefixed

## Evaluation

6. [[Project/Benchmarking]] — workload phases, metrics, and how to read the results honestly

## Scope

7. [[Project/Gaps and Future Work]] — what is missing, what to build next, in priority order

---

## 30-second summary

```
client ──TCP──▶ RedisServer ──▶ RedisCommandHandler ──▶ RedisDatabase
                  (threads,          (RESP parse,          (one keyspace,
                   sockets)           dispatch)             one mutex,
                                                          buffer pool)
                                                              │
                                                      EvictionEngine
                                                   (policy + capacity
                                                    + counters)
```

| Component | Files | Role |
| --- | --- | --- |
| `RedisServer` | `include/RedisServer.h`, `src/RedisServer.cpp` | Sockets, accept loop, thread-per-connection, snapshot thread |
| `RedisCommandHandler` | `include/RedisCommandHandler.h`, `src/RedisCommandHandler.cpp` | RESP parsing and dispatch |
| `RedisDatabase` | `include/RedisDatabase.h`, `src/RedisDatabase.cpp` | The buffer pool: one keyspace, one mutex, TTLs, snapshots |
| `EvictionEngine` | `include/eviction/eviction_engine.h`, `src/eviction/eviction_engine.cpp` | Policy factory, capacity limit, counters |
| Policies | `include/eviction/*_policy.h`, `src/eviction/*.cpp` | LRU, LFU, FIFO, Random, Adaptive |

---

## Related

- [[OS Index]]
- [[DBMS Index]]