---
tags: [dbms, transactions, acid, atomicity, durability, isolation, project/adaptive-cache-redis]
status: not-implemented
---

# Transactions and ACID

## Definition

A **transaction** is a logical unit of work. ACID:

| Property | Meaning | Enforcement mechanism |
| --- | --- | --- |
| **Atomicity** | All or nothing | Undo log, rollback |
| **Consistency** | Constraints hold before and after | Constraints + the other three |
| **Isolation** | Concurrent transactions appear serial | Locking, MVCC, snapshot isolation |
| **Durability** | Committed changes survive a crash | Write-ahead log + `fsync` |

A transaction moves through states — **active → partially committed →
committed → aborted** — and may fail at any point.

---

## In this project

> [!warning] Not implemented
> There are no transactions. `MULTI`/`EXEC`/`DISCARD` do not exist, a single
> command is the unit of work, and partial command failures are possible in
> principle — although each command here is individually atomic.

### What *is* atomic, and why

Every public `RedisDatabase` method takes `db_mutex` and mutates state entirely
within that scope:

`src/RedisDatabase.cpp:324`
```cpp
OpStatus RedisDatabase::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();
    ...
    registerInsertLocked(key, key.size() + value.size(), isNew);
    return OpStatus::Ok;
}
```

So a single command cannot interleave with another, and no client can observe a
half-applied command. That is **command-level atomicity**, which is weaker than
transaction-level atomicity: nothing groups several commands.

> [!info] The closest thing to a transaction
> `HMSET` and `RPUSH` accept multiple field/value pairs and apply them in one
> call, so they are atomic across arguments. That is a command with several
> sub-operations, not a transaction mechanism.

### Isolation: trivially serial

The single global mutex means all operations are serialised. Under the standard
isolation-level hierarchy that is stronger than *serializable* — the most
isolated level there is.

> [!warning] But it is isolation by exclusion, not by design
> There is no MVCC, no snapshot, no versioning. Every reader blocks every writer
> and vice versa. That buys correctness at the cost of concurrency, and it does
> not scale — which is precisely why real systems implement isolation properly
> instead of globally locking.

### Durability: snapshot, not log

`src/RedisDatabase.cpp:862`–`src/RedisDatabase.cpp:927` writes the whole
dataset to a temp file and renames it.

| ACID | Status |
| --- | --- |
| Atomicity | Partial — per command only |
| Consistency | Not applicable — no constraints declared |
| Isolation | Strong, via one global mutex |
| Durability | **Deferred** — up to the snapshot interval of lost writes |

> [!tip] Honest framing
> "There are no transactions. Writes become durable at the next snapshot, up to
> 300 seconds later, so durability is eventual rather than immediate. Adding a
> write-ahead log would close that gap."

---

## Not implemented

> [!warning] Absent
> - `MULTI` / `EXEC` / `DISCARD` / `WATCH`
> - Undo and redo logs — see [[DBMS/Recovery and Logging]]
> - Constraint checking (unique, foreign key, check)
> - Savepoints and partial rollback
> - Transaction IDs or timestamps

---

## Related

- [[DBMS/Concurrency Control]] — how isolation is actually achieved
- [[DBMS/Recovery and Logging]] — atomicity and durability
- [[OS/Mutual Exclusion]] — the mutex that accidentally provides serialisation
- [[Project/Gaps and Future Work]] — WAL as the highest-value extension