# Operating System (OS) Concepts in My Cpp Redis Server

This document outlines the core **Operating System (OS)** principles and low-level systems programming concepts implemented in `my_redis_server`, mapped directly to their implementations in the codebase.

---

## Architecture Overview

```
                      +-----------------------------+
                      |   Client TCP Connections    |
                      +--------------+--------------+
                                     |
                                     v
+------------------------------------+--------------------------------------+
|                     Linux / POSIX Kernel Space                            |
|                                                                           |
|   +-------------------+   +--------------------+   +------------------+   |
|   | Socket Buffers    |   | Thread Scheduler   |   | Hardware Clocks  |   |
|   | (recv/send queue) |   | (CPU Time Slices)  |   | (CLOCK_MONOTONIC)|   |
|   +---------+---------+   +---------+----------+   +--------+---------+   |
+-------------|-----------------------|-----------------------|-------------+
              |                       |                       |
              v                       v                       v
+-------------|-----------------------|-----------------------|-------------+
|             |             User Space Application            |             |
|             |                       |                       |             |
|   +---------v-----------+           |              +--------v---------+   |
|   | RedisServer         |           |              | Expiry Engine    |   |
|   | (accept() loop)     |           |              | (steady_clock)   |   |
|   +---------+-----------+           |              +--------+---------+   |
|             |                       |                       |             |
|             v                       v                       |             |
|   +-------------------+    +-------------------+            |             |
|   | Client Threads    |--->|  db_mutex (Mutex) |<-----------+             |
|   | (Thread-per-conn) |    +---------+---------+                          |
|   +---------+---------+              |                                    |
|             |                        v                                    |
|             |              +-------------------+                          |
|             +------------->| RedisDatabase     |                          |
|                            | (kv, list, hash)  |                          |
|                            +---------+---------+                          |
|                                      |                                    |
|                                      v                                    |
|                            +-------------------+                          |
|                            | dump.my_rdb       | (Disk File Persistence)  |
|                            +-------------------+                          |
+---------------------------------------------------------------------------+
```

---

## 1. Concurrency & Multithreading

### A. Thread-per-Connection Model
* **Concept:** Multi-client concurrency where each client connection runs in parallel without blocking other active clients.
* **Implementation:** [`src/RedisServer.cpp`](src/RedisServer.cpp)
* **Code Reference:**
  ```cpp
  // Inside RedisServer::run()
  const int client_socket = accept(server_socket, nullptr, nullptr);
  std::thread(serveClient, clientSocket, std::cref(running)).detach();
  ```
* **OS Mechanics:** When `accept()` returns a new file descriptor, the main thread creates a new kernel-scheduled thread (`std::thread`). The OS scheduler time-slices these threads across available CPU cores.

### B. Background Daemon / Detached Worker Thread
* **Concept:** Independent background task execution that does not block the primary application thread.
* **Implementation:** [`src/RedisServer.cpp`](src/RedisServer.cpp)
* **Code Reference:**
  ```cpp
  persistThread = std::thread([this]() {
      constexpr auto slice = std::chrono::milliseconds(200);
      const auto interval = std::chrono::seconds(persistIntervalSeconds);
      auto elapsed = std::chrono::steady_clock::duration::zero();

      while (persistRunning.load()) {
          std::this_thread::sleep_for(slice);
          elapsed += slice;
          if (elapsed < interval) continue;
          elapsed = std::chrono::steady_clock::duration::zero();
          if (!persistRunning.load()) break;
          persistNow();
      }
  });
  ```
* **OS Mechanics:** The thread sleeps in 200 ms slices rather than one long sleep, so the shutdown flag is noticed promptly instead of after a full interval. It is a named `std::thread` member rather than a detached local, so `stopPersistence()` can join it before the process exits. The previous detached loop could never be stopped and kept dumping during teardown.

### C. Thread Joining & Lifecycle Cleanup
* **Concept:** Process teardown synchronization to ensure worker threads conclude cleanly without orphaned resources.
* **Implementation:** [`src/RedisServer.cpp`](src/RedisServer.cpp)
* **Code Reference:**
  ```cpp
  void RedisServer::stopPersistence() {
      persistRunning.store(false);
      if (persistThread.joinable()) persistThread.join();
  }
  ```
* **Trade-off:** Connection handlers are detached rather than joined, so the server does not accumulate one `std::thread` object per connection ever made (the benchmark client opens a fresh connection per command). The cost is that in-flight handlers are not drained at exit; `run()` allows a short grace period before tearing down. Client handlers are free functions taking the running flag by reference, so a detached handler can never dereference a destroyed `RedisServer`.

---

## 2. Synchronization & Mutual Exclusion (Race Condition Prevention)

### A. Mutex & Critical Sections
* **Concept:** Mutual exclusion primitive preventing race conditions when multiple threads concurrently read and write to shared memory stores.
* **Implementation:** [`include/RedisDatabase.h`](include/RedisDatabase.h) & [`src/RedisDatabase.cpp`](src/RedisDatabase.cpp)
* **Code Reference:**
  ```cpp
  std::mutex db_mutex; // declared in RedisDatabase.h

  void RedisDatabase::set(const std::string& key, const std::string& value) {
      std::lock_guard<std::mutex> lock(db_mutex); // Critical Section
      Value entry;
      entry.type = Value::Type::String;
      entry.str = value;
      store[key] = std::move(entry);
      registerInsertLocked(key, key.size() + value.size(), isNew);
  }
  ```
* **OS Mechanics:** If Thread B attempts to lock `db_mutex` while Thread A holds it, Thread B transitions from `RUNNING` to `BLOCKED/WAITING` in the OS scheduler queue until Thread A releases the lock.

### B. RAII Lock Management (`std::lock_guard`)
* **Concept:** Resource Acquisition Is Initialization (RAII) ensures mutex release upon exiting scope (even during exceptions or early returns), avoiding deadlocks.

### C. Atomic Variables (`std::atomic`)
* **Concept:** Hardware-level atomic operations allowing lock-free reads and writes for lightweight synchronization flags.
* **Implementation:** [`include/RedisServer.h`](include/RedisServer.h)
* **Code Reference:**
  ```cpp
  std::atomic<bool> running;
  ```
* **OS Mechanics:** Modifying `running` emits memory barrier CPU instructions ensuring visibility across CPU caches without the overhead of context switches or mutex acquisition.

---

## 3. Networking & Inter-Process Communication (IPC via POSIX Sockets)

Implemented in [`src/RedisServer.cpp`](src/RedisServer.cpp) using the standard POSIX Berkeley Sockets API:

| Socket System Call | OS Functionality & Role |
| :--- | :--- |
| `socket(AF_INET, SOCK_STREAM, 0)` | Allocates a stream socket file descriptor backed by kernel TCP buffers. |
| `setsockopt(..., SO_REUSEADDR, ...)` | Configures socket flags to bypass the OS kernel `TIME_WAIT` state, allowing immediate port reuse on server restarts. |
| `bind()` | Maps the socket file descriptor to a network interface (`INADDR_ANY`) and port (`6379`). |
| `listen(server_socket, 128)` | Transitions socket to passive listening mode and sets the kernel backlog queue size. |
| `accept()` | Blocks until the 3-way TCP handshake completes, returning a new dedicated connection descriptor. |
| `recv()` / `send()` | Reads from and writes to kernel TCP socket buffers across user-space and kernel-space boundaries. |
| `close()` | Decrements kernel socket file descriptor refcount, triggering TCP teardown (`FIN`/`ACK`). |

---

## 4. Signals & Asynchronous Interrupt Handling

### A. Kernel Signal Trapping (`SIGINT`)
* **Concept:** Inter-process communication and software interrupts to handle external OS events (such as `Ctrl + C`).
* **Implementation:** [`src/RedisServer.cpp`](src/RedisServer.cpp)
* **Code Reference:**
  ```cpp
  void signalHandler(int) {
      if (!globalServer) return;
      globalServer->requestShutdown();
  }

  void RedisServer::requestShutdown() {
      running.store(false);
      const int fd = listen_fd.load();
      if (fd != -1) ::shutdown(fd, SHUT_RDWR);  // unblocks accept()
  }
  ```
* **OS Mechanics:** When the terminal sends `SIGINT`/`SIGTERM`, the kernel halts the current instruction sequence and redirects the thread to `signalHandler()`. The handler is async-signal-safe: it performs no allocation, no iostream writes and no `exit()`. The earlier version called `std::cout` and `exit()` from the handler, both of which are undefined in a signal context and could deadlock on the stdio lock.

### B. Graceful Shutdown & Persistence
* **Concept:** Orderly application termination that saves dirty state to disk and closes network descriptors before returning control to the OS.
* **Code Reference:**
  ```cpp
  // requestShutdown() only flags and unblocks accept(); run() then finishes up:
  stopPersistence();                       // join the snapshot thread
  close(server_socket);                    // release the listening FD
  persistNow();                            // one final snapshot
  ```
* **Note:** the snapshot is written exactly once. The previous code dumped from both the signal handler and the end of `run()`, so every `Ctrl+C` wrote the file twice.

---

## 5. File System & Secondary Storage Persistence

* **Concept:** Virtual File System (VFS) abstractions and persistent storage across process life cycles.
* **Implementation:** [`src/RedisDatabase.cpp`](src/RedisDatabase.cpp)
* **Code Reference:**
  ```cpp
  bool RedisDatabase::dump(const std::string& filename) {
      std::ofstream ofs(filename, std::ios::binary);
      // Writes K (key-value), L (list), and H (hash) records
  }

  bool RedisDatabase::load(const std::string& filename) {
      std::ifstream ifs(filename, std::ios::binary);
      // Restores state upon server boot
  }
  ```
* **OS Mechanics:** Transfers user-space memory buffers into OS disk cache pages, which the kernel flushes to persistent block storage (`dump.my_rdb`).

---

## 6. Time, Clocks & Scheduling

### A. Monotonic Clock vs. Wall Clock
* **Concept:** Preventing time-drift anomalies in TTL expiration caused by system wall-clock adjustments (e.g. NTP updates).
* **Implementation:** [`include/RedisDatabase.h`](include/RedisDatabase.h) & [`src/RedisDatabase.cpp`](src/RedisDatabase.cpp)
* **Code Reference:**
  ```cpp
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry_map;

  // In expire():
  expiry_map[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  ```
* **OS Mechanics:** `std::chrono::steady_clock` relies on hardware tick registers / `clock_gettime(CLOCK_MONOTONIC)`, guaranteeing strictly forward-advancing time.

### B. Passive / Lazy Eviction Strategy
* **Concept:** Reducing OS scheduling and interrupt overhead by deferring expiration cleanups until keys are accessed.
* **Code Reference:**
  ```cpp
  void RedisDatabase::purgeExpiredLocked() {
      if (expiry_map.empty()) return;
      const auto now = Clock::now();

      // Collect first, then remove: removeKeyLocked() erases from expiry_map
      // itself, so erasing during the walk would leave a dangling iterator.
      std::vector<std::string> expired;
      for (const auto& entry : expiry_map) {
          if (now > entry.second) expired.push_back(entry.first);
      }
      for (const auto& key : expired) {
          removeKeyLocked(key, RemovalReason::Expired);
      }
  }
  ```

### C. Kernel Scheduler Sleep States
* **Concept:** Voluntarily relinquishing CPU time slices.
* **Code Reference:**
  ```cpp
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ```
* **OS Mechanics:** Transitions the thread from the OS `RUNNING` queue to `SLEEPING/WAITING`, consuming 0% CPU until the kernel hardware timer interrupt fires.

---

## 7. Stream Framing & Application Protocol Parsing

* **Concept:** Managing byte-stream boundaries over stream-oriented transport protocols (TCP).
* **Implementation:** [`src/RedisCommandHandler.cpp`](src/RedisCommandHandler.cpp)
* **Code Reference:**
  ```cpp
  ParseResult parseRespCommand(const std::string& buffer, std::size_t& consumed,
                               std::vector<std::string>& tokens);
  ```
  ```cpp
  // Inside serveClient(): accumulate, then drain every complete command.
  pending.append(chunk.data(), static_cast<std::size_t>(bytes));

  std::size_t offset = 0;
  while (true) {
      std::size_t consumed = 0;
      bool closeAfter = false;
      const std::string reply =
          handler.processCommand(pending.substr(offset), consumed, closeAfter);
      offset += consumed;
      if (!reply.empty() && !sendAll(clientSocket, reply)) break;
      if (closeAfter) break;
      if (consumed == 0) break;   // incomplete: wait for more bytes
  }
  pending.erase(0, offset);
  ```
* **OS Mechanics:** TCP is a continuous byte stream with no packet boundaries. The parser delimits messages using CRLF terminators and `$<len>` length prefixes, and reports how many bytes it consumed so the caller can keep the remainder for the next command.
* **Why the buffer matters:** the previous design did a single `recv()` into a fixed `char[1024]` and treated the whole chunk as one command. A command split across two TCP segments was silently mangled, and anything over ~1000 bytes was truncated. Each handler now owns a `std::string` that grows to hold a partial command, which also makes pipelined commands work, with a 16 MB ceiling so a client that never completes a line cannot exhaust server memory.
