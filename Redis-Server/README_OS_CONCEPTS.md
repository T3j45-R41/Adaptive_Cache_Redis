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
  int client_socket = accept(server_socket, nullptr, nullptr);
  threads.emplace_back([client_socket, &cmdHandler](){
      char buffer[1024];
      while (true) {
          int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
          if (bytes <= 0) break;
          std::string request(buffer, bytes);
          std::string response = cmdHandler.processCommand(request);
          send(client_socket, response.c_str(), response.size(), 0);
      }
      close(client_socket);
  });
  ```
* **OS Mechanics:** When `accept()` returns a new file descriptor, the main thread creates a new kernel-scheduled thread (`std::thread`). The OS scheduler time-slices these threads across available CPU cores.

### B. Background Daemon / Detached Worker Thread
* **Concept:** Independent background task execution that does not block the primary application thread.
* **Implementation:** [`src/main.cpp`](src/main.cpp)
* **Code Reference:**
  ```cpp
  std::thread persistanceThread([](){
      while (true) {
          std::this_thread::sleep_for(std::chrono::seconds(300));
          RedisDatabase::getInstance().dump("dump.my_rdb");
      }
  });
  persistanceThread.detach();
  ```
* **OS Mechanics:** Calling `.detach()` separates the thread of execution from the thread object, allowing it to execute as an independent daemon managed directly by the OS runtime until the process exits.

### C. Thread Joining & Lifecycle Cleanup
* **Concept:** Process teardown synchronization to ensure worker threads conclude cleanly without orphaned resources.
* **Implementation:** [`src/RedisServer.cpp`](src/RedisServer.cpp)
* **Code Reference:**
  ```cpp
  for (auto& t : threads) {
      if (t.joinable()) t.join();
  }
  ```

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
      kv_store[key] = value;
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
| `listen(server_socket, 10)` | Transitions socket to passive listening mode and sets the kernel backlog queue size (10 connections). |
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
  void signalHandler(int signum) {
      if (globalServer) {
          globalServer->shutdown();
      }
      exit(signum);
  }

  void RedisServer::setupSignalHandler() {
      signal(SIGINT, signalHandler);
  }
  ```
* **OS Mechanics:** When the terminal sends `SIGINT`, the kernel halts the current CPU instruction sequence and redirects the thread to `signalHandler()`.

### B. Graceful Shutdown & Persistence
* **Concept:** Orderly application termination that saves dirty state to disk and closes network descriptors before returning control to the OS.
* **Code Reference:**
  ```cpp
  void RedisServer::shutdown() {
      running = false;
      if (server_socket != -1) {
          RedisDatabase::getInstance().dump("dump.my_rdb"); // Flush to disk
          close(server_socket);                             // Release socket FD
      }
  }
  ```

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
  void RedisDatabase::purgeExpired() {
      auto now = std::chrono::steady_clock::now();
      for (auto it = expiry_map.begin(); it != expiry_map.end(); ) {
          if (now > it->second) {
              kv_store.erase(it->first);
              list_store.erase(it->first);
              hash_store.erase(it->first);
              it = expiry_map.erase(it);
          } else {
              ++it;
          }
      }
  }
  ```

### C. Kernel Scheduler Sleep States
* **Concept:** Voluntarily relinquishing CPU time slices.
* **Code Reference:**
  ```cpp
  std::this_thread::sleep_for(std::chrono::seconds(300));
  ```
* **OS Mechanics:** Transitions the thread from the OS `RUNNING` queue to `SLEEPING/WAITING`, consuming 0% CPU until the kernel hardware timer interrupt fires.

---

## 7. Stream Framing & Application Protocol Parsing

* **Concept:** Managing byte-stream boundaries over stream-oriented transport protocols (TCP).
* **Implementation:** [`src/RedisCommandHandler.cpp`](src/RedisCommandHandler.cpp)
* **Code Reference:**
  ```cpp
  std::vector<std::string> parseRespCommand(const std::string &input);
  ```
* **OS Mechanics:** TCP is a continuous byte stream with no packet boundaries. The application-level parser delimits messages by parsing CRLF (`\r\n`) terminators and payload length prefixes (`$<len>`), reconstructing discrete commands from the 1024-byte read buffer.
