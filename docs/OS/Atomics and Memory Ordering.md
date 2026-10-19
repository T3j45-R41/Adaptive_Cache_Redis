---
tags: [os, atomics, memory-model, concurrency, project/adaptive-cache-redis]
status: implemented
---

# Atomics and Memory Ordering

## Definition

A **data race** under the C++ memory model is a conflict on a non-atomic
variable where at least one access is a write, with no happens-before relation
between them. The consequence is officially **undefined behaviour** — the
compiler is entitled to hoist loads out of loops, cache a value in a register,
or reorder writes.

Atomics fix this by guaranteeing both **atomicity** (the read or write happens
once, indivisibly) and **ordering** (a release write is visible before a
subsequent acquire read).

Memory-ordering levels, weakest to strongest:

| Ordering | Guarantees |
| --- | --- |
| `relaxed` | Atomic, no ordering |
| `acquire` load / `release` store | One-way fence: everything before a release is visible after a matching acquire |
| `seq_cst` (default) | Total order across all atomics |

## When an atomic is enough

A mutex is correct for multi-variable invariants — several fields that must
change together. An atomic is correct for a single flag whose only job is to be
seen. Using a mutex for a flag costs a syscall-free but real futex round trip
when contended; a relaxed atomic is a single instruction.

---

## In this project

### The shutdown flag

`include/RedisServer.h`
```cpp
std::atomic<bool> running;
std::atomic<int> listen_fd;
```

Used from three places: the signal handler, the accept loop, and every handler
thread.

`src/RedisServer.cpp:126`
```cpp
void RedisServer::requestShutdown() {
    running.store(false);
    const int fd = listen_fd.load();
    if (fd != -1) ::shutdown(fd, SHUT_RDWR); // unblocks accept()
}
```

`src/RedisServer.cpp:52`–`src/RedisServer.cpp:54`
```cpp
void serveClient(int clientSocket, const std::atomic<bool>& running) {
    ...
    while (running.load()) {
```

> [!important] Why this is async-signal-safe
> A lock-free `std::atomic<bool>` and `std::atomic<int>` store compile to plain
> instructions on x86-64 — no allocation, no lock, no library call. That is
> exactly what makes them legal inside a signal handler, where a mutex is not.
> See [[OS/Signals and Interrupts]].

### The persistence stop flag

`include/RedisServer.h`
```cpp
std::atomic<bool> persistRunning;
```

Polled by the snapshot thread so it can exit promptly:

`src/RedisServer.cpp:177`–`src/RedisServer.cpp:192`
```cpp
persistThread = std::thread([this]() {
    constexpr auto slice = std::chrono::milliseconds(200);
    const auto interval = std::chrono::seconds(persistIntervalSeconds);
    auto elapsed = std::chrono::steady_clock::duration::zero();

    while (persistRunning.load()) {
        std::this_thread::sleep_for(slice);
        elapsed += slice;
        if (elapsed < interval) continue;
        ...
    }
});
```

> [!tip] Why sleep in slices
> A single `sleep_for(300s)` would not notice the flag for up to five minutes.
> Polling every 200 ms makes shutdown feel immediate at a cost of 5 wakeups
> per second on one thread.

### What is *not* atomic

> [!warning] One lock, one keyspace
> The dataset itself is protected by `db_mutex`, not by atomics. The policy
> weights in `AdaptivePolicy` are plain `double`s and are safe only because
> every read and write of them happens under `db_mutex`.
>
> This is the important distinction: the atomics coordinate *lifecycle*, the
> mutex protects *data*.

---

## A bug this concept exposes

> [!warning] Passing a flag by reference to a detached thread
> The first draft captured the server object by member access from a detached
> thread. A detached thread outliving its owner is a use-after-free that no
> compiler will warn about, because nothing in the *type* is wrong — only the
> lifetime.
>
> The fix was to pass `std::atomic<bool>` **by reference** to a free function,
> so the thread never touches the `RedisServer`.

---

## Related

- [[OS/Mutual Exclusion]] — the heavier tool
- [[OS/Signals and Interrupts]] — where signal-safety actually matters
- [[OS/Processes and Threads]] — detached threads and lifetimes
- [[OS/Clocks and Time]] — `steady_clock` is also chosen for defined behaviour