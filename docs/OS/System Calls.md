---
tags: [os, system-calls, kernel, performance, project/adaptive-cache-redis]
status: implemented
---

# System Calls

## Definition

A **system call** is a controlled transition from user space into the kernel.
The CPU switches privilege level, the kernel validates arguments, does the work,
and returns. The transition costs on the order of hundreds of nanoseconds —
orders of magnitude more than a function call, because the CPU's caches and
branch predictors are cold on the other side.

**Trap vs. interrupt:** a *system call* is a synchronous, voluntary trap; an
*interrupt* (like a network packet arriving) is asynchronous. Both cross the
same boundary.

---

## In this project

The server is thin enough that nearly all its cost is boundary crossings.

| Operation | System calls | Frequency |
| --- | --- | --- |
| Accept a connection | `accept` | once per client |
| Send one command | `recv` + `send` | once per command |
| Store access | **none** | `unordered_map` lookup is pure userspace |
| TTL check | **none** | `steady_clock::now()` is a `clock_gettime` call |
| Snapshot | `open`/`write`/`close` per record | once per interval |

### The hot path is deliberately cheap

> [!important] The single most consequential design choice
> Because the whole dataset is in RAM and served from an `unordered_map`, a
> cache **hit costs zero system calls** beyond the socket read. Eviction is also
> userspace.
>
> This is precisely why the buffer-pool abstraction is worth studying: the
> entire point of caching is to turn an I/O operation into an in-memory lookup.
> See [[DBMS/Buffer Pool]].

### Large reads amortise the transition

`src/RedisServer.cpp`
```cpp
constexpr std::size_t kReadChunk = 8192;
```

Reading in 8 KB chunks rather than one byte at a time keeps the
user↔kernel ratio low. The previous 1024-byte buffer made the syscall cost
proportionally eight times higher.

### `clock_gettime` is a real syscall

`steady_clock::now()` resolves to `clock_gettime(CLOCK_MONOTONIC)` — typically
served by the vDSO, but not guaranteed. It is called inside the critical
section on most operations via `purgeExpiredLocked()`.

> [!abstract] Micro-optimisation not taken
> `purgeExpiredLocked()` returns immediately when `expiry_map` is empty, which
> is the common case. That guard avoids the `clock_gettime` entirely for
> workloads that never use TTLs — which is all of them, until the benchmark
> seeds one.

`src/RedisDatabase.cpp:148`–`src/RedisDatabase.cpp:149`
```cpp
if (expiry_map.empty()) return;
const auto now = Clock::now();
```

---

## Not implemented

> [!warning] Absent
> - **`epoll`/`select`/`kqueue`.** Each handler thread blocks in `recv()`.
>   With 1,000 idle connections, 1,000 threads are blocked in the kernel.
>   A single event loop would use one thread regardless of connection count.
> - **`io_uring`.** No asynchronous submission.
> - **`sendfile`/`splice`.** Responses are copied userspace → kernel → userspace.
> - **Batch commands.** Each command is a separate `send`. Pipelining reduces
>   the round trips but the responses are still written individually.
> - **`getrusage`/`/proc` instrumentation.** The project has no syscall or page-
>   fault counters of its own.

---

## Related

- [[OS/POSIX Sockets]] — the dominant syscall group
- [[OS/System Calls]] ↔ [[OS/Clocks and Time]] — the `clock_gettime` path
- [[DBMS/Buffer Pool]] — why eliminating I/O is the whole point
- [[Project/Gaps and Future Work]] — the `epoll` extension