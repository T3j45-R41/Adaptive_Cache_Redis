---
tags: [moc, os, project/adaptive-cache-redis]
status: index
---

# OS Index

Operating-systems concepts used by **Adaptive Cache Redis**, each linked to the
code that exercises it.

> [!tip] For a viva
> The strongest OS answers in this project are the ones with a *non-obvious
> reason attached*: why `SIGPIPE` is ignored, why the signal handler cannot call
> `std::cout`, and why `steady_clock` is required for TTLs. Those show
> understanding rather than recall.

---

## Process and communication

1. [[Processes and Threads]] — thread-per-connection, detached workers, the accept loop
2. [[POSIX Sockets]] — `socket`/`bind`/`listen`/`accept`, the file-descriptor model
3. [[Signals and Interrupts]] — `SIGINT`, `SIGTERM`, `SIGPIPE`, async-signal safety
4. [[Stream Framing]] — TCP is a byte stream; the length-prefixed protocol on top

## Synchronisation

5. [[Mutual Exclusion]] — one global `std::mutex`, RAII, why the store is a single keyspace
6. [[Atomics and Memory Ordering]] — `std::atomic<bool>`, lock-free flags vs. mutexes

## Memory and storage

7. [[Virtual Memory]] — process address space, heap allocation, what "in memory" means
8. [[File Systems and VFS]] — `ofstream`/`ifstream`, atomic rename, crash-safe writes

## The kernel boundary

9. [[System Calls]] — user/kernel transition cost, why I/O batching matters
10. [[Clocks and Time]] — `steady_clock` vs. wall clock, and why TTLs demand monotonic time

---

## Which OS concepts are *absent*?

> [!warning] Not implemented
> - **Scheduling / fairness** — `std::thread` is used but no policy is set;
>   the OS scheduler decides. Nothing in the code controls it.
> - **IPC beyond sockets** — no shared memory, pipes, or message queues.
>   All client communication is TCP.
> - **Multiprocessing** — single process, threads only. No `fork()`, no
>   worker processes.
> - **Virtual memory management of the store** — the hash map is ordinary heap;
>   no `mmap`, no explicit paging, no memory-mapped files.
> - **Security** — no authentication, no privilege dropping, no TLS.
>   Any client that can reach the port has full control of the dataset.

---

## Related

- [[DBMS Index]] — the storage-management side
- [[Project/Concept-to-Code Map]] — one table for every concept