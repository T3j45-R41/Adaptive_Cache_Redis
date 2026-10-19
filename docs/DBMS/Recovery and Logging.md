---
tags: [dbms, recovery, wal, checkpointing, aries, durability, project/adaptive-cache-redis]
status: not-implemented
---

# Recovery and Logging

## Definition

A **write-ahead log (WAL)** records every change before it is applied, so that
after a crash the database can be reconstructed. The **write-ahead logging
rule**:

> A log record describing a change must reach stable storage *before* the data
> page itself does.

Two operations follow from a crash:

| Operation | Purpose |
| --- | --- |
| **Redo** | Re-apply committed changes lost after the last checkpoint |
| **Undo** | Roll back uncommitted changes of failed transactions |

**ARIES** (Algorithm for Recovery and Isolation Exceeding Redo) is the
reference implementation: an analysis pass reconstructs the transaction table
from the log, a redo pass repeats history (idempotently, from a checkpoint's
LSN), and an undo pass rolls back losers, following the log backwards.

**Checkpointing** periodically writes a consistent snapshot so recovery does not
have to scan the entire log.

---

## In this project

> [!warning] Not implemented
> There is **no log**. No redo, no undo, no checkpoint record, no LSN, no
> recovery on startup. The server restores from a periodic full snapshot and
> loses everything written since.

### What exists instead: atomic snapshotting

`src/RedisDatabase.cpp:914`–`src/RedisDatabase.cpp:927`
```cpp
ofs.flush();
if (!ofs) { ofs.close(); removeFile(temp); return false; }
ofs.close();

removeFile(filename);
if (!renameFile(temp, filename)) {
    removeFile(temp);
    return false;
}
```

This is a **checkpoint in the informal sense** — a consistent point-in-time
image, made safe to publish by atomic rename. What it lacks is everything that
makes recovery incremental:

| ARIES / WAL | This project |
| --- | --- |
| Append-only log | Full rewrite of the dataset |
| Redo only committed updates | Replay the whole file |
| Undo uncommitted work | Nothing to undo — writes are visible immediately |
| `fsync` before the page write | `flush()` only; no `fsync` |
| Checkpoint LSN | None |
| Recovery cost proportional to damage | Recovery cost proportional to file size |

> [!warning] The two real durability gaps
> **1. No `fsync`.** `ofs.flush()` pushes to the OS page cache, not to the
> device. A power failure can lose the snapshot entirely.
>
> **2. Interval loss.** With the default 300-second interval, a crash can lose
> up to five minutes of writes. `INFO write_backs` counts dirty-frame
> evictions, so that exposure is measurable — but not yet prevented.

### The dirty bit, ready for a log

`include/eviction/cache_entry.h`
```cpp
// Modified since the last snapshot write-back. An evicted dirty frame costs
// one write I/O; a clean one costs nothing.
bool dirty{false};
```

and `src/RedisDatabase.cpp:925`
```cpp
for (auto& entry : frames) entry.second.dirty = false;
```

> [!info] The hook already exists
> A dirty bit cleared at checkpoint boundaries is exactly the metadata a WAL
> needs. Extending it is natural: append `<LSN, key, oldValue, newValue>` to a
> log on every mutation, `fsync` before responding, and stop rewriting the
> whole dataset.

---

## Why this is the highest-value extension

> [!tip] Ranking
> Adding a WAL would demonstrate **three** DBMS topics at once — atomicity
> (undo), durability (redo + the write-ahead rule), and recovery (ARIES analysis
> pass) — from infrastructure that is already present (the dirty bit, the
> logical clock, and the staged-commit pattern used in `load()`).
>
> It is also self-demonstrating: kill the server mid-run with `kill -9`,
> restart, and show that nothing committed was lost.

---

## Related

- [[DBMS/Transactions and ACID]] — what recovery protects
- [[DBMS/File Organization]] — pages would be the units logged
- [[OS/File Systems and VFS]] — atomic rename, and the missing `fsync`
- [[DBMS/Buffer Pool]] — where the dirty bit lives
- [[Project/Gaps and Future Work]] — the extension plan