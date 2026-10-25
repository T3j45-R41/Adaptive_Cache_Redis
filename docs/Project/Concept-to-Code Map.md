---
tags: [project/adaptive-cache-redis, reference, index, moca]
status: reference
---

# Concept-to-Code Map

Every concept the project uses, and exactly where it lives. Paths are relative
to the repository root.

---

## Operating Systems

| Concept | Where | Key detail |
| --- | --- | --- |
| [[OS/Processes and Threads\|Threads]] | `src/RedisServer.cpp:52` `serveClient` | Thread-per-connection, detached; `serveClient` is a **free function** so a detached thread cannot touch a destroyed server |
| [[OS/Processes and Threads\|Thread creation]] | `src/RedisServer.cpp:227` | `std::thread(serveClient, fd, std::cref(running)).detach()` |
| [[OS/Processes and Threads\|Joined worker]] | `src/RedisServer.cpp:177`, `:198` | Snapshot thread is a named member and is joined |
| [[OS/POSIX Sockets\|Socket setup]] | `src/RedisServer.cpp:133`–`:155` | `socket` → `setsockopt` → `bind` → `listen(128)` |
| [[OS/POSIX Sockets\|`SO_REUSEADDR`]] | `src/RedisServer.cpp:140` | Avoids `TIME_WAIT` blocking restarts |
| [[OS/POSIX Sockets\|Byte order]] | `src/RedisServer.cpp:144` | `htons()` converts the port to network order |
| [[OS/POSIX Sockets\|Partial write loop]] | `src/RedisServer.cpp:35`–`:46` | `sendAll()` resumes on `EINTR` and short writes |
| [[OS/POSIX Sockets\|`MSG_NOSIGNAL`]] | `src/RedisServer.cpp:38` | Prevents `SIGPIPE` killing the process |
| [[OS/Signals and Interrupts\|Signal install]] | `src/RedisServer.cpp:117`–`:123` | `SIGPIPE` ignored; `SIGINT`/`SIGTERM` via `sigaction`, **no** `SA_RESTART` |
| [[OS/Signals and Interrupts\|Async-signal-safe handler]] | `src/RedisServer.cpp:29`–`:32` | Atomic store + `shutdown()`; no iostream, no `exit()` |
| [[OS/Signals and Interrupts\|Unblocking accept]] | `src/RedisServer.cpp:126`–`:130` | `requestShutdown()` sets the flag and calls `::shutdown(fd, SHUT_RDWR)` |
| [[OS/Stream Framing\|Per-connection buffer]] | `src/RedisServer.cpp:52`–`:96` | Accumulate, drain every complete command, keep the tail |
| [[OS/Stream Framing\|Buffer ceiling]] | `src/RedisServer.cpp:52` | `kMaxPendingBytes = 16 MB` |
| [[OS/Stream Framing\|Wire bounds]] | `src/RedisCommandHandler.cpp:32`–`:33` | Caps on element count and bulk length |
| [[OS/Stream Framing\|Invalid-frame recovery]] | `src/RedisCommandHandler.cpp:69`–`:71` | `consumed = buffer.size()` — non-zero, or the loop spins forever |
| [[OS/Mutual Exclusion\|Global mutex]] | `include/RedisDatabase.h` | `mutable std::mutex db_mutex` |
| [[OS/Mutual Exclusion\|RAII locking]] | `src/RedisDatabase.cpp:324` et al. | `std::lock_guard` on every public entry point |
| [[OS/Mutual Exclusion\|Locked-helper convention]] | whole of `src/RedisDatabase.cpp` | `...Locked()` suffix prevents re-entrant self-deadlock |
| [[OS/Mutual Exclusion\|Collect-then-remove]] | `src/RedisDatabase.cpp:148`–`:162` | Fixes a use-after-free ASan caught in the expiry sweep |
| [[OS/Atomics and Memory Ordering\|Shutdown flag]] | `include/RedisServer.h` | `std::atomic<bool> running`, `std::atomic<int> listen_fd` |
| [[OS/Atomics and Memory Ordering\|Snapshot flag]] | `include/RedisServer.h` | `std::atomic<bool> persistRunning`, polled every 200 ms |
| [[OS/Virtual Memory\|Heap store]] | `src/RedisDatabase.cpp` | One `unordered_map`; no `mmap`, no memory limit |
| [[OS/Virtual Memory\|Byte accounting]] | `src/RedisDatabase.cpp` `valueBytesLocked()` | Reported, **never enforced** |
| [[OS/Virtual Memory\|Logical clocks]] | `include/eviction/cache_entry.h` | Deterministic replay |
| [[OS/File Systems and VFS\|Temp-then-rename]] | `src/RedisDatabase.cpp:914`–`:927` | Crash-safe atomic publish |
| [[OS/File Systems and VFS\|Clear dirty bits]] | `src/RedisDatabase.cpp:925` | Makes `write_backs` meaningful |
| [[OS/File Systems and VFS\|Staged load]] | `src/RedisDatabase.cpp:932`–`:975` | A bad file leaves live data intact |
| [[OS/System Calls\|Zero-syscall hits]] | `src/RedisDatabase.cpp:324` | A cache hit is an in-memory probe |
| [[OS/System Calls\|Large reads]] | `src/RedisServer.cpp` | `kReadChunk = 8192` |
| [[OS/Clocks and Time\|Monotonic TTLs]] | `src/RedisDatabase.cpp` | `steady_clock` for `expiry_map` |
| [[OS/Clocks and Time\|Empty-expiry fast path]] | `src/RedisDatabase.cpp:148`–`:149` | Avoids `clock_gettime` entirely |
| [[OS/Clocks and Time\|TTL return codes]] | `src/RedisDatabase.cpp` | `-2` missing, `-1` no expiry |

---

## Database Management Systems

| Concept | Where | Status |
| --- | --- | --- |
| [[DBMS/Buffer Pool\|Buffer pool]] | `src/RedisDatabase.cpp` `store` + `frames` | ✅ Implemented |
| [[DBMS/Buffer Pool\|Frame table]] | `include/eviction/cache_entry.h` | ✅ Implemented |
| [[DBMS/Buffer Pool\|Buffer manager]] | `src/RedisDatabase.cpp:110`–`:129` | ✅ Implemented |
| [[DBMS/Buffer Pool\|Dirty bit]] | `include/eviction/cache_entry.h` `dirty` | ✅ Implemented |
| [[DBMS/Buffer Pool\|Pin count]] | — | ❌ Not needed; no long-running operations |
| [[DBMS/Buffer Replacement Policies\|Policy interface]] | `include/eviction/eviction_policy.h` | ✅ Implemented |
| [[DBMS/Buffer Replacement Policies\|LRU]] | `src/eviction/lru_policy.cpp` | ✅ `list` + iterator map, O(1) |
| [[DBMS/Buffer Replacement Policies\|LFU]] | `src/eviction/lfu_policy.cpp:4` | ✅ O(n) scan, recency tiebreak |
| [[DBMS/Buffer Replacement Policies\|FIFO]] | `src/eviction/fifo_policy.cpp` | ✅ `deque` + tombstone skipping |
| [[DBMS/Buffer Replacement Policies\|Random]] | `src/eviction/random_policy.cpp` | ✅ Null model |
| [[DBMS/Adaptive Replacement\|Adaptive controller]] | `src/eviction/adaptive_policy.cpp` | ✅ Two experts + decayed regret |
| [[DBMS/Adaptive Replacement\|Weight floor]] | `include/eviction/adaptive_policy.h:58` | ✅ `kMinWeight_ = 0.05` |
| [[DBMS/Adaptive Replacement\|CacheUS proper]] | — | ❌ Not implemented |
| [[DBMS/Buffer Replacement Policies\|CLOCK / 2nd chance]] | — | ❌ Not implemented |
| [[DBMS/Indexing\|Hash index]] | `src/RedisDatabase.cpp` `store` | ✅ Implicit |
| [[DBMS/Indexing\|Glob key scan]] | `src/RedisDatabase.cpp:172` | ✅ Full scan, pattern honoured |
| [[DBMS/Indexing\|B+ tree]] | — | ❌ Not implemented |
| [[DBMS/Indexing\|Range scans]] | — | ❌ Not implemented |
| [[DBMS/File Organization\|Length-prefixed records]] | `src/RedisDatabase.cpp:807`–`:845` | ✅ Partial (not slotted pages) |
| [[DBMS/File Organization\|Slotted pages]] | — | ❌ Not implemented |
| [[DBMS/Concurrency Control\|Coarse exclusive lock]] | `include/RedisDatabase.h` | ✅ 2PL, one lock |
| [[DBMS/Concurrency Control\|Deadlock freedom]] | structural | ✅ By construction |
| [[DBMS/Concurrency Control\|MVCC]] | — | ❌ Not implemented |
| [[DBMS/Transactions and ACID\|Command atomicity]] | `src/RedisDatabase.cpp:324` et al. | ✅ Per command only |
| [[DBMS/Transactions and ACID\|`MULTI`/`EXEC`]] | — | ❌ Not implemented |
| [[DBMS/Recovery and Logging\|Atomic snapshot]] | `src/RedisDatabase.cpp:862`–`:927` | ✅ Checkpoint-like |
| [[DBMS/Recovery and Logging\|Write-ahead log]] | — | ❌ Not implemented |
| [[DBMS/Recovery and Logging\|`fsync`]] | — | ❌ Missing |
| [[DBMS/Query Processing\|Command dispatch]] | `src/RedisCommandHandler.cpp` | ✅ Dispatch exists |
| [[DBMS/Query Processing\|Parser / optimiser]] | — | ❌ Not implemented |
| [[DBMS/Normalization\|Workload schema analysis]] | this vault, [[DBMS/Normalization]] | 📄 Documented only |

---

## Statistics and observability

| Concept | Where |
| --- | --- |
| Hit/miss counters | `include/eviction/eviction_metrics.h` → `INFO` |
| [[DBMS/Buffer Pool\|Write-back counting]] | `eviction_metrics.h` `writes` → `INFO write_backs` |
| Per-policy victim attribution | `src/eviction/eviction_engine.cpp` `bumpPolicySelection()` |
| Adaptive weights exposure | `src/eviction/eviction_engine.cpp` `policyDetail()` → `INFO adaptive_detail` |
| Expiration / delete counters | `eviction_metrics.h` → `INFO` |

---

## Tests

| Suite | File | Covers |
| --- | --- | --- |
| Policy unit tests | `Redis-Server/tests/test_eviction.cpp` | All five policies, engine, metrics — 1069 checks |
| Integration | `Redis-Server/tests/test_integration.cpp` | Commands, types, eviction, persistence, RESP framing, concurrency — 180 checks |

Both run under ASan, UBSan and TSan with zero findings.

---

## Related

- [[Project/Architecture]]
- [[Project/Gaps and Future Work]] — everything marked ❌, prioritised