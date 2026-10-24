---
tags: [os, synchronization, mutex, race-condition, raii, project/adaptive-cache-redis]
status: implemented
---

# Mutual Exclusion

## Definition

A **race condition** occurs when the result of concurrent access to shared data
depends on the unsynchronised interleaving of threads. The classic
interleaving problem is check-then-act:

```
Thread A: if (!seen) { count++; }
Thread B: if (!seen) { count++; }
```

Both may read `seen == false` before either writes it, and `count` ends up 1
instead of 2.

**Mutual exclusion** guarantees that only one thread is inside a critical
section at a time.

## RAII

**Resource Acquisition Is Initialization** ties a resource's lifetime to a
scope. `std::lock_guard` acquires the mutex in its constructor and releases it
in its destructor, so the lock is released on every exit path — normal return,
early return, or exception — with no manual bookkeeping.

---

## In this project

### One mutex for the whole store

`include/RedisDatabase.h`
```cpp
// mutable: the read-only accessors still need to take the lock, because
// every public entry point serialises against the writers.
mutable std::mutex db_mutex;
```

Every public operation takes it:

`src/RedisDatabase.cpp:324`–`src/RedisDatabase.cpp:334`
```cpp
OpStatus RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);   // Critical Section
    purgeExpiredLocked();

    const bool isNew = store.find(key) == store.end();
    Value entry;
    entry.type = Value::Type::String;
    entry.str = value;
    store[key] = std::move(entry);

    registerInsertLocked(key, key.size() + value.size(), isNew);
    return OpStatus::Ok;
}
```

> [!tip] Why `mutable`?
> Read-only accessors (`dbSize()`, `type()`, `ttl()`) still need to lock,
> because "reading" is not atomic with respect to a concurrent writer. Without
> `mutable`, the compiler forbids locking in a `const` method. This is the one
> legitimate reason to break const-correctness here, and it is worth being able
> to explain.

### The re-entrancy trap

Every helper called while the lock is held is suffixed `Locked`:

```
purgeExpiredLocked()   removeKeyLocked()   enforceCapacityLocked()
registerInsertLocked() registerAccessLocked()   resyncPolicyLocked()
```

> [!warning] A real bug this convention prevents
> `configGet()` originally called the public `totalKeyBytes()`, which tries to
> lock the same non-recursive mutex → self-deadlock. The suffix is a compiler-
> enforced reminder that these are the unlocked internals.

### Design choice: one keyspace, one lock

The original code kept three parallel maps:
```cpp
std::unordered_map<std::string, std::string> kv_store;
std::unordered_map<std::string, std::vector<std::string>> list_store;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_store;
```

A single key could therefore exist in all three at once. `KEYS` returned
duplicates, `DEL` counted once while erasing three times, and capacity
accounting was meaningless.

It is now one tagged store:

`src/RedisDatabase.cpp` — `struct Value`
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

One map means one invariant to maintain and one lock to take.

### Race-free iterator handling in expiry

> [!warning] A use-after-free caught by AddressSanitizer
> The first version of the rewritten expiry sweep erased while iterating:
> ```cpp
> for (auto it = expiry_map.begin(); it != expiry_map.end();) {
>     if (now > it->second) {
>         removeKeyLocked(it->first, RemovalReason::Expired);  // erases from expiry_map!
>         it = expiry_map.erase(it);                          // dangling iterator
>     }
> }
> ```
> `removeKeyLocked()` removes the entry from `expiry_map` itself, so the
> subsequent `erase(it)` operated on freed memory.

`src/RedisDatabase.cpp:148`–`src/RedisDatabase.cpp:162`
```cpp
void RedisDatabase::purgeExpiredLocked() {
    if (expiry_map.empty()) return;
    const auto now = Clock::now();

    // Collect first, then remove. removeKeyLocked() erases the entry from
    // expiry_map itself, so erasing during the walk would leave a dangling iterator.
    std::vector<std::string> expired;
    for (const auto& entry : expiry_map) {
        if (now > entry.second) expired.push_back(entry.first);
    }
    for (const auto& key : expired) {
        removeKeyLocked(key, RemovalReason::Expired);
    }
}
```

---

## What is verified

The test suite runs under **ThreadSanitizer** and reports zero data races,
including a concurrency test with 6 threads × 25 write/read round trips over
real sockets.

`Redis-Server/tests/test_integration.cpp` → `testConcurrentClients`

---

## Limitations

> [!warning] Not implemented
> - **Fine-grained locking.** One mutex serialises every operation. No
>   per-shard or per-bucket locks.
> - **Reader/writer split.** Read-heavy workloads (`KEYS *`, `HGETALL`) block
>   writers entirely.
> - **Lock-free structures.** No concurrent hash map, no RCU, no hazard
>   pointers.
> - **Deadlock avoidance by design.** Currently safe only because there is
>   exactly one lock and it is never held while calling out. Adding a second
>   lock would immediately require a fixed ordering discipline.

> [!important] Honest framing
> The global mutex is the project's dominant performance ceiling, and it is a
> deliberate simplification, not a scalable design. Say so before being asked.

---

## Related

- [[DBMS/Concurrency Control]] — the DBMS framing of the same problem
- [[OS/Atomics and Memory Ordering]] — when a mutex is too heavy
- [[DBMS/Buffer Pool]] — what is actually being protected
- [[Project/Architecture]] — the request path