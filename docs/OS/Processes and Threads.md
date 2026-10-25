---
tags: [os, process, threading, concurrency, project/adaptive-cache-redis]
status: implemented
---

# Processes and Threads

## Definition

A **process** is an address space plus at least one thread of execution. A
**thread** is an independently scheduled stream of instructions within a process.
Threads in the same process share one address space, so they share every
variable, but each has its own program counter, stack, and registers.

Concurrency therefore has two independent axes:
- **Intra-process** — threads, sharing memory, risking races.
- **Inter-process** — separate address spaces, communicating via IPC.

This project uses threads only.

---

## In this project

### Thread-per-connection

Every accepted connection gets its own thread, which lives for the duration of
that connection.

`src/RedisServer.cpp:213`
```cpp
const int clientSocket = accept(server_socket, nullptr, nullptr);
...
std::thread(serveClient, clientSocket, std::cref(running)).detach();
```

`serveClient` is a **free function**, not a member. That is deliberate: the
threads are detached, so a handler must never dereference a `RedisServer` that
`run()` may already have destroyed.

`src/RedisServer.cpp:52`
```cpp
// Deliberately a free function taking the shared flag by reference: these
// threads are detached, so they must not dereference a RedisServer that may
// already have been destroyed by the time run() returns.
void serveClient(int clientSocket, const std::atomic<bool>& running) {
```

### Why detach instead of collecting

> [!abstract] Partial
> The original code pushed every thread into a `std::vector` and joined them all
> at shutdown. Since `Benchmark/benchmark_client.py` opens **a fresh TCP
> connection per command**, a 300-operation run creates 300 `std::thread`
> objects that are all retained until the process exits. Detaching drops that
> bookkeeping.
>
> The trade-off: in-flight handlers are not drained at shutdown, so `run()`
> allows a fixed grace period instead.
>
> `src/RedisServer.cpp:231`
> ```cpp
> std::this_thread::sleep_for(std::chrono::milliseconds(100));
> ```

### A joinable background thread

The snapshot thread is the opposite case — it *is* a named member and *is*
joined, because an unstoppable background thread would keep writing to disk
during teardown.

`src/RedisServer.cpp:177`, `src/RedisServer.cpp:198`
```cpp
persistThread = std::thread([this]() { /* ... */ });
...
if (persistThread.joinable()) persistThread.join();
```

> [!warning] Not implemented
> - **Scheduling policy.** `std::thread` is created with default attributes.
>   Nothing sets affinity or priority; the OS scheduler decides.
> - **Thread pool.** One OS thread per connection. With thousands of
>   simultaneous clients this is expensive — each thread reserves a stack.
> - **Multiprocessing.** No `fork()`, no worker processes, no `SO_REUSEPORT`
>   sharding across CPU cores.

---

## Why it matters

Creating a kernel thread is far more expensive than a system call: the kernel
allocates a stack, creates a task structure, and schedules it. That cost is
amortised across an entire connection here, which is why thread-per-connection
is defensible for this workload but would not survive a large deployment.

> [!tip] Viva answer
> "Each connection is handled by one `std::thread`, so a slow client cannot
> block others. The accept loop only dispatches; all actual work happens in the
> handler. Handler threads are detached to avoid retaining a `std::thread`
> object per connection, and the snapshot thread is joined at exit so it cannot
> outlive the process."

---

## Related

- [[OS/Mutual Exclusion]] — what protects the data these threads share
- [[OS/Atomics and Memory Ordering]] — the lock-free shutdown flag
- [[OS/Signals and Interrupts]] — how the accept loop is stopped
- [[OS/System Calls]] — thread creation is a system call
- [[DBMS/Concurrency Control]] — the DBMS view of the same problem