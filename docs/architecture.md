# System Architecture: Adaptive Cache Redis & MySQL DBMS

This document details the complete end-to-end multi-tier architecture of the project.

---

## 1. High-Level Architecture Diagram

```
+-----------------------------------------------------------------------------------+
|                            HISTORICAL STOCK MARKET INGESTION                      |
|                                                                                   |
|    Yahoo Finance API (yfinance)  --->  Geometric Brownian Motion (GBM Fallback)   |
|                                    |                                              |
|                                    v                                              |
|                      Relational Database (stock_data)                             |
+------------------------------------+----------------------------------------------+
                                     |
                                     v
+------------------------------------+----------------------------------------------+
|                          STOCK WORKLOAD GENERATOR                                 |
|                                                                                   |
|  - Zipfian (α = 1.2)           - Bursty Poisson / Pareto     - Changing Popularity|
|  - Popularity Spikes (Flash)   - 80/20 Hot-Cold Key Access   - Normal Gaussian    |
+------------------------------------+----------------------------------------------+
                                     |
                                     v
+------------------------------------+----------------------------------------------+
|                    BENCHMARK CLIENT (Python Monotonic Timer)                      |
|                                                                                   |
|           RESP2 Protocol over TCP Stream Socket (Framing & Pipelining)            |
+------------------------------------+----------------------------------------------+
                                     |
                                     v
+------------------------------------+----------------------------------------------+
|               CUSTOM C++ REDIS-COMPATIBLE IN-MEMORY SERVER                        |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  | Socket Transport (Winsock2 / POSIX Sockets) + Non-blocking / Bounded Pool   |  |
|  +-------------------------------------+---------------------------------------+  |
|                                        |                                          |
|                                        v                                          |
|  +-----------------------------------------------------------------------------+  |
|  | Streaming RESP2 Message Parser & Command Dispatcher                         |  |
|  | (Pipelining, Partial Frame Buffering, SET/GET/LRANGE/HSET/TTL/INFO/CONFIG)  |  |
|  +-------------------------------------+---------------------------------------+  |
|                                        |                                          |
|                                        v                                          |
|  +-----------------------------------------------------------------------------+  |
|  | In-Memory Cache Store (std::shared_mutex Concurrency)                       |  |
|  | (Key-Value, Lists, Hashes, Expiry Map, Steady Clock Passive Purge)           |  |
|  +-------------------------------------+---------------------------------------+  |
|                                        |                                          |
|                                        v                                          |
|  +-----------------------------------------------------------------------------+  |
|  | PLUGGABLE MODULAR EVICTION ENGINE                                           |  |
|  |                                                                             |  |
|  |  [LRU]      Doubly Linked List + Hash Map (O(1) Recency)                    |  |
|  |  [LFU]      Frequency Bucket Doubly Linked Lists (O(1) Frequency)           |  |
|  |  [FIFO]     Chronological Insertion Queue                                   |  |
|  |  [RANDOM]   Uniform mt19937 PRNG with Swap-and-Pop Deletion                 |  |
|  |  [ADAPTIVE] CACHEUS Dual Shadow Experts + Hedge Online Regret Minimization   |  |
|  +-------------------------------------+---------------------------------------+  |
|                                        |                                          |
|                                        v                                          |
|  +-----------------------------------------------------------------------------+  |
|  | Background Persistence Daemon (Periodic binary RDB dump.my_rdb snapshots)   |  |
|  +-----------------------------------------------------------------------------+  |
+------------------------------------+----------------------------------------------+
                                     | Performance Metrics & Audit Logs
                                     v
+------------------------------------+----------------------------------------------+
|                     MYSQL 8.x RELATIONAL PERSISTENCE LAYER                        |
|                                                                                   |
|  - Normalized Tables: benchmark_results, cache_metrics, eviction_logs             |
|  - Analytical SQL Views: vw_policy_performance, vw_experiment_comparison          |
|  - Stored Procedures: sp_get_experiment_summary, sp_compare_eviction_policies     |
|  - Database Triggers: Audit logs on experiment status updates                      |
+------------------------------------+----------------------------------------------+
                                     |
                                     v
+------------------------------------+----------------------------------------------+
|                       PYTHON FASTAPI REST BACKEND                                 |
|                                                                                   |
|  Endpoints: /api/dashboard, /api/cache, /api/experiments, /api/stocks, /api/mysql  |
+------------------------------------+----------------------------------------------+
                                     |
                                     v
+------------------------------------+----------------------------------------------+
|                 REACT 18 + VITE + TAILWIND MONITORING DASHBOARD                   |
|                                                                                   |
|  Interactive Metric Cards, Recharts Comparison Matrix, Adaptive Regret Charts     |
+-----------------------------------------------------------------------------------+
```

---

## 2. Separation of Architectural Concerns

1. **Caching Tier (Custom C++)**:
   Responsible for ultra-low latency, in-memory key-value lookups, list/hash operations, bounded capacity management, and dynamic eviction algorithm execution.

2. **Persistence & Analytics Tier (MySQL 8.x)**:
   Responsible for persistent historical stock records, structured experiment traces, comparative benchmarking matrix, and query execution auditing.

3. **Workload & Benchmarking Engine (Python)**:
   Generates deterministic access traces with controlled skew, measures high-resolution latency using `time.perf_counter_ns`, computes temporal staleness, and evaluates policies side-by-side.

4. **Presentation Tier (FastAPI + React)**:
   Provides live telemetry, interactive charts, metric sorting, and CSV data export for academic presentation.
