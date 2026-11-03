# Operating Systems Concepts in the Custom C++ Redis Server

This document maps core Operating System (OS) principles and low-level systems programming concepts directly to their actual source-code implementations in the C++ server.

---

## 1. Concurrency & Multithreading

### A. Bounded Worker Thread Pool (Producer-Consumer Model)
- **Concept**: Unbounded thread creation (`thread-per-connection`) exhausts kernel stack memory and leads to excessive context switching. A bounded worker thread pool bounds concurrency and uses a synchronized task queue.
- **Source Code**: [`Redis-Server/src/RedisServer.cpp`](../Redis-Server/src/RedisServer.cpp)
- **Implementation**:
  - Main listener thread executes `accept()` and pushes accepted socket descriptors into `clientQueue`.
  - Condition variable `queueCv.notify_one()` signals worker threads.
  - Bounded pool of $N$ worker threads wait on `queueCv.wait()`, pop descriptors, and execute the client session loop.

### B. Producer-Consumer Synchronization (`std::condition_variable` & `std::mutex`)
- **Source Code**: `RedisServer::workerThreadLoop()`
- **Mechanics**:
  ```cpp
  std::unique_lock<std::mutex> lock(queueMutex);
  queueCv.wait(lock, [this]() {
      return !running || !clientQueue.empty();
  });
  ```
  Transitions worker threads from the OS `RUNNING` queue to `WAITING` queue with zero CPU consumption until work arrives.

### C. Background Daemon Thread (Joined vs Detached)
- **Source Code**: [`Redis-Server/src/main.cpp`](../Redis-Server/src/main.cpp)
- **Implementation**:
  - Launches a periodic snapshotting thread that saves dirty state to disk every 300 seconds.
  - Unlike detached threads which can be rudely killed during process teardown, the server gracefully joins the persistence thread upon shutdown:
    ```cpp
    if (persistenceThread.joinable()) persistenceThread.join();
    ```

---

## 2. Synchronization & Mutual Exclusion (Race Condition Prevention)

### A. Reader/Writer Locks (`std::shared_mutex`)
- **Concept**: In high-concurrency caching servers, read operations (`GET`, `LRANGE`, `HGET`, `HEXISTS`) vastly outnumber writes (`SET`, `DEL`). A regular `std::mutex` forces serialized reads. A shared mutex allows multiple threads to read concurrently while granting exclusive access to writers.
- **Source Code**: [`Redis-Server/include/RedisDatabase.h`](../Redis-Server/include/RedisDatabase.h) & [`Redis-Server/src/RedisDatabase.cpp`](../Redis-Server/src/RedisDatabase.cpp)
- **Implementation**:
  - Reads acquire `std::shared_lock<std::shared_mutex> lock(db_mutex);`
  - Writes/Evictions acquire `std::unique_lock<std::shared_mutex> lock(db_mutex);`

### B. Atomic Variables (`std::atomic<bool>`)
- **Source Code**: `std::atomic<bool> running;` in `RedisServer.h`
- **OS Mechanics**: Emits hardware memory barrier instructions (`LOCK` prefix on x86_64) ensuring visibility across CPU caches without the overhead of mutex acquisition or context switches.

### C. RAII Resource Management
- **Concept**: Resource Acquisition Is Initialization.
- **Implementation**: All locks use scoped guards (`std::lock_guard`, `std::unique_lock`, `std::shared_lock`), ensuring automatic mutex unlocking even during early returns or exception unwinding.

---

## 3. Network IPC & Transport Protocol Framing

### A. Berkeley Sockets API (Cross-Platform Winsock2 & POSIX)
- **Source Code**: [`Redis-Server/src/RedisServer.cpp`](../Redis-Server/src/RedisServer.cpp)
- **System Calls**:
  - `socket(AF_INET, SOCK_STREAM, 0)`: Allocates stream socket file descriptor.
  - `setsockopt(..., SO_REUSEADDR, ...)`: Bypasses OS kernel `TIME_WAIT` socket delay on restart.
  - `bind()`: Binds socket to interface and port.
  - `listen(..., 128)`: Sets kernel TCP backlog queue.
  - `accept()`: Completes 3-way TCP handshake.
  - `recv()` / `send()`: Kernel buffer data transfer.
  - `closesocket()` / `close()`: TCP teardown (`FIN`/`ACK`).

### B. TCP Stream Framing & Pipelining Parser
- **Concept**: TCP is a stream-oriented transport with no message boundaries. A single `recv()` can return a partial command or multiple pipelined commands.
- **Source Code**: `RedisCommandHandler::extractNextCommand()`
- **Implementation**:
  - Maintains persistent per-connection input buffer.
  - Parses RESP length-prefixed arrays (`*<count>\r\n$<len>\r\n`).
  - Only consumes complete frames, leaving partial remainders buffered for subsequent `recv()` calls.

---

## 4. Time, Clocks & Scheduling

### A. Monotonic Clock (`CLOCK_MONOTONIC` / `std::chrono::steady_clock`)
- **Concept**: Wall clocks (`std::chrono::system_clock`) are susceptible to NTP time adjustments and daylight savings shifts, which would corrupt cache TTL calculations.
- **Source Code**: `RedisDatabase::expire()`, `RedisDatabase::ttl()`
- **Mechanics**: Relies on strictly monotonically increasing hardware tick registers.

### B. Lazy / Passive Expiration Strategy
- **Source Code**: `RedisDatabase::purgeExpired()`
- **Mechanics**: Expired keys are lazily purged upon access or mutation to avoid periodic kernel interrupt overhead.

---

## 5. File System Persistence

### A. Secondary Storage Snapshotting (VFS Page Cache Flushes)
- **Source Code**: `RedisDatabase::dump()` and `RedisDatabase::load()`
- **Mechanics**: Compact binary snapshot format (`dump.my_rdb`) writing directly to file streams, transferred by the OS VFS layer to disk blocks.
