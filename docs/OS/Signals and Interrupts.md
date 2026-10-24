---
tags: [os, signals, interrupts, async, project/adaptive-cache-redis]
status: implemented
---

# Signals and Interrupts

## Definition

A **signal** is a software interrupt delivered asynchronously by the kernel to a
process. It suspends whatever the thread was doing, runs a handler, and resumes.

Signals used here:
- `SIGINT` — Ctrl+C in a terminal.
- `SIGTERM` — polite termination request from `kill` or a supervisor.
- `SIGPIPE` — raised when writing to a socket whose peer has gone away.

---

## The async-signal-safe rule

> [!warning] The single most important idea on this page
> A signal handler runs on an arbitrary thread at an arbitrary point, including
> **in the middle of a `malloc`**. Therefore only *async-signal-safe* operations
> are legal inside it.
>
> **Not safe:** `std::cout`, `printf` with buffering, `malloc`, any C++ container
> method, `exit()`, throwing, or taking a lock that another thread might hold.
>
> **Safe:** assigning an `std::atomic` of a lock-free type, `write()` to an fd,
> setting/clearing an errno variable, `_exit()`.

---

## In this project

### Before: unsafe

The original handler called `std::cout` and `exit()`:

```cpp
void signalHandler(int signum) {
    if (globalServer) {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}
```

Three distinct bugs:
1. `std::cout` allocates on first use and locks a stream mutex — if the signal
   landed inside an iostream operation, it deadlocks against itself.
2. `exit()` runs static destructors and flushes streams, none of which is
   async-signal-safe.
3. `shutdown()` wrote a snapshot to disk **and** the end of `run()` wrote it
   again, so every Ctrl+C saved twice.

### After: safe

`src/RedisServer.cpp:126`–`src/RedisServer.cpp:130`
```cpp
void RedisServer::requestShutdown() {
    running.store(false);
    const int fd = listen_fd.load();
    if (fd != -1) ::shutdown(fd, SHUT_RDWR); // unblocks accept()
}
```

`src/RedisServer.cpp:29`–`src/RedisServer.cpp:32`
```cpp
// Async-signal-safe only: no allocation, no iostream writes, no exit().
void signalHandler(int) {
    if (!globalServer) return;
    globalServer->requestShutdown();
}
```

The handler now does exactly two things: store to a lock-free atomic, and call
`shutdown()` on a descriptor. **All real work happens later**, on the accept
loop thread, after `accept()` returns.

### Unblocking `accept()`

> [!important] The subtle part
> `accept()` blocks. Setting a flag does nothing until a connection arrives.
> Calling `shutdown()` on the listening socket makes the pending `accept()`
> fail immediately with `EINTR` or `EINVAL`, so the loop can notice the flag.
>
> The listening descriptor is mirrored into an `std::atomic<int>`
> (`listen_fd`) precisely so the handler can reach it without touching the
> `RedisServer` object's non-atomic fields.

`src/RedisServer.cpp:121`
```cpp
action.sa_flags = 0; // no SA_RESTART: accept() must return so we can exit
```

> [!tip] Why `SA_RESTART` is deliberately omitted
> With `SA_RESTART`, the kernel silently restarts interrupted syscalls, so
> `accept()` would resume blocking and the shutdown flag would never be
> observed. Clearing it is what makes Ctrl+C actually work.

### `SIGPIPE`

`src/RedisServer.cpp:115`–`src/RedisServer.cpp:117`
```cpp
// SIGPIPE would otherwise kill the process when a client disconnects
// mid-write; MSG_NOSIGNAL handles it at the send() call site.
std::signal(SIGPIPE, SIG_IGN);
```

Defence in depth: the signal is ignored globally *and* each `send()` passes
`MSG_NOSIGNAL`. See [[OS/POSIX Sockets]].

### Signals in tests

The signal handlers are installed by `RedisServer`'s constructor, so a test
binary that constructs a server inherits them. `timeout(1)` sends `SIGTERM` by
default; because the handler swallows it, the process must be killed with
`timeout -k`.

---

## Related

- [[OS/Processes and Threads]] — which thread runs the handler
- [[OS/Atomics and Memory Ordering]] — why `std::atomic` is the right type here
- [[OS/POSIX Sockets]] — `MSG_NOSIGNAL`
- [[Project/Architecture]] — the shutdown sequence end to end