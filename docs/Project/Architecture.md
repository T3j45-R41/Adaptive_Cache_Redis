---
tags: [project/adaptive-cache-redis, architecture, overview]
status: implemented
---

# Architecture

## What the system is

A Redis-compatible server whose central abstraction is a **buffer pool**:
an in-memory keyspace that discards entries when it reaches a configured limit,
using a swappable replacement policy.

```
                       ┌──────────────────────────────────────────┐
   TCP client  ──────▶ │  RedisServer                             │
                       │  socket/bind/listen/accept              │
                       │  thread-per-connection, detached         │
                       │  SIGINT/SIGTERM → graceful shutdown     │
                       └───────────────────┬──────────────────────┘
                                           │ RESP bytes
                       ┌───────────────────▼──────────────────────┐
                       │  RedisCommandHandler                     │
                       │  parse multibulk | inline → tokens      │
                       │  dispatch by name → RESP reply          │
                       └───────────────────┬──────────────────────┘
                                           │ typed calls
                       ┌───────────────────▼──────────────────────┐
                       │  RedisDatabase      [db_mutex]           │
                       │  store : key → Value{String|List|Hash}  │
                       │  frames : key → CacheEntry              │
                       │  expiry_map : key → deadline            │
                       │  capacity enforcement, snapshots        │
                       └───────────────────┬──────────────────────┘
                                           │ onInsert/onAccess/onRemove
                                           │ selectVictim()
                       ┌───────────────────▼──────────────────────┐
                       │  EvictionEngine                         │
                       │  policy factory · capacity · counters   │
                       │  ┌────────┬────────┬───────┬─────────┐  │
                       │  │  LRU   │  LFU   │ FIFO  │ ADAPTIVE│  │
                       │  └────────┴────────┴───────┴─────────┘  │
                       └──────────────────────────────────────────┘
```

## The request path

`src/RedisServer.cpp:52` → `src/RedisCommandHandler.cpp:60` → `src/RedisDatabase.cpp`

1. `accept()` returns a new descriptor.
2. A detached thread runs `serveClient`.
3. Bytes accumulate in a per-connection `std::string`.
4. `parseRespCommand` pulls one command, reporting `consumed` bytes.
5. Dispatch reaches a `handle*` function.
6. The database method locks `db_mutex`, purges expired keys, mutates, and
   notifies the eviction engine.
7. If over capacity, the engine picks a victim and the key is removed.
8. The reply is encoded to RESP and written with `sendAll`.

---

## Storage layout

> [!important] One keyspace, one type per key
> The original design used three parallel maps, so a single key could hold a
> string, a list **and** a hash simultaneously. `KEYS` returned duplicates,
> `DEL` counted once while erasing three times, and capacity accounting was
> meaningless.

```cpp
struct Value {
    enum class Type { String, List, Hash };
    Type type{Type::String};
    std::string str;
    std::vector<std::string> list;
    std::unordered_map<std::string, std::string> hash;
};
std::unordered_map<std::string, Value> store;
```

Now `GET` on a list key returns `-ERR wrongtype ...`, exactly as Redis does.

---

## Design decisions and their reasons

| Decision | Reason |
| --- | --- |
| One global mutex | Simplest correct option; a deliberate scalability ceiling |
| Hash index only | The workload is entirely point lookups |
| Capacity in keys, not bytes | Simpler; byte accounting is reported but not enforced |
| Lazy TTL expiry | No background scan; matches how the access pattern works |
| Snapshot, not WAL | Simple and crash-safe at the file level; loses up to one interval |
| Length-prefixed text records | Binary-safe and human-readable; not slotted pages |
| Detached client threads | Avoids retaining a `std::thread` per connection ever made |
| Joined snapshot thread | Must not outlive the process |
| Logical clocks in policies | Deterministic benchmarks |

---

## Threading

| Thread | Created | Lifetime | Sync |
| --- | --- | --- | --- |
| Main / accept loop | `main()` | Process | — |
| Client handler | per `accept()` | Connection, detached | `db_mutex` |
| Snapshot | `run()` | Process, joined | `db_mutex`, `persistRunning` |

`std::atomic<bool> running` and `std::atomic<int> listen_fd` coordinate shutdown
without a lock. See [[OS/Atomics and Memory Ordering]].

---

## Errors

All replies use `-ERR <detail>`, including wrong-type errors. Real Redis uses
`-WRONGTYPE`, but the project's own clients detect errors by the leading `-`,
so a single prefix keeps `ui_app.py` and `benchmark_client.py` correct.

```cpp
case OpStatus::NotFound:  return "no such key";
case OpStatus::WrongType: return "wrongtype operation against a key "
                               "holding the wrong kind of value";
```

---

## Related

- [[Project/Concept-to-Code Map]]
- [[Project/Eviction Engine]]
- [[Project/RESP Protocol]]
- [[Project/Persistence Format]]
- [[DBMS/Buffer Pool]] — the abstraction the whole design rests on