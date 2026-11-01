---
tags: [dbms, sqlite, relational, schema, integration, project/adaptive-cache-redis]
status: implemented
---

# Phase 3: SQLite Database Integration (DBMS Coursework)

## Overview

This module integrates an **SQLite Relational Database** into the `Adaptive_Cache_Redis` repository, replacing flat CSV files with an ACID-compliant relational schema, advanced views, and triggers while keeping existing core files intact and backwards-compatible.

---

## 1. Relational Database Schema

The database consists of 4 normalized tables with primary and foreign key constraints:

### A. `stocks` Table (Historical Quotes)
* **Functional Dependency**: `(ticker, timestamp) → close, volume`
* **Normal Form**: **3NF** (Composite Primary Key eliminates update/insertion anomalies).
* **Schema**:
  ```sql
  CREATE TABLE stocks (
      ticker TEXT NOT NULL,
      timestamp TEXT NOT NULL,
      close REAL NOT NULL,
      volume INTEGER DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
      PRIMARY KEY (ticker, timestamp)
  );
  CREATE INDEX idx_stocks_ticker_ts ON stocks (ticker, timestamp DESC);
  ```

### B. `cache_keys` Table (Cache Persistence)
* **Schema**:
  ```sql
  CREATE TABLE cache_keys (
      key TEXT PRIMARY KEY,
      value TEXT,
      key_type TEXT NOT NULL DEFAULT 'string',
      ttl INTEGER DEFAULT -1,
      byte_size INTEGER DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
      updated_at TEXT NOT NULL DEFAULT (DATETIME('now'))
  );
  CREATE INDEX idx_cache_keys_updated_at ON cache_keys (updated_at DESC);
  ```

### C. `benchmarks` Table (Benchmark Experiment Runs)
* **Schema**:
  ```sql
  CREATE TABLE benchmarks (
      run_id TEXT PRIMARY KEY,
      run_label TEXT NOT NULL,
      policy TEXT NOT NULL,
      concurrency INTEGER NOT NULL,
      total_operations INTEGER NOT NULL,
      success_count INTEGER NOT NULL,
      error_count INTEGER NOT NULL,
      cache_hits INTEGER NOT NULL,
      cache_misses INTEGER NOT NULL,
      hit_rate_percent REAL NOT NULL,
      wall_time_seconds REAL NOT NULL,
      throughput_ops_sec REAL NOT NULL,
      avg_latency_ms REAL NOT NULL,
      median_latency_ms REAL NOT NULL,
      p95_latency_ms REAL NOT NULL,
      p99_latency_ms REAL NOT NULL,
      min_latency_ms REAL,
      max_latency_ms REAL,
      server_evictions INTEGER,
      server_hits INTEGER,
      server_misses INTEGER,
      server_hit_rate_percent REAL,
      max_keys INTEGER,
      hit_target_percent REAL,
      created_at TEXT NOT NULL DEFAULT (DATETIME('now'))
  );
  ```

### D. `eviction_logs` Table (Per-Operation Trace & Evictions)
* **Foreign Key**: `FOREIGN KEY (run_id) REFERENCES benchmarks(run_id) ON DELETE CASCADE`
* **Schema**:
  ```sql
  CREATE TABLE eviction_logs (
      log_id INTEGER PRIMARY KEY AUTOINCREMENT,
      run_id TEXT NOT NULL,
      operation_index INTEGER NOT NULL,
      key TEXT,
      operation TEXT NOT NULL,
      policy TEXT,
      cache_hit INTEGER NOT NULL,
      cache_miss INTEGER NOT NULL,
      latency_ms REAL NOT NULL,
      staleness_seconds REAL,
      evicted_key TEXT,
      eviction_reason TEXT,
      timestamp TEXT NOT NULL DEFAULT (DATETIME('now')),
      FOREIGN KEY (run_id) REFERENCES benchmarks(run_id) ON DELETE CASCADE
  );
  CREATE INDEX idx_eviction_logs_run_id ON eviction_logs (run_id, operation_index);
  ```

---

## 2. Advanced DBMS Features

### SQL Views

1. **`latest_stock_quotes`**
   Provides instantaneous access to the latest quote for every stock without duplicating rows or scanning the whole table:
   ```sql
   CREATE VIEW latest_stock_quotes AS
   SELECT s.ticker, s.timestamp, s.close, s.volume, s.created_at
   FROM stocks s
   INNER JOIN (
       SELECT ticker, MAX(timestamp) AS max_timestamp
       FROM stocks
       GROUP BY ticker
   ) latest 
       ON s.ticker = latest.ticker 
      AND s.timestamp = latest.max_timestamp;
   ```

2. **`policy_performance_summary`**
   Aggregates benchmark metrics across eviction policies (LRU, LFU, FIFO, ADAPTIVE):
   ```sql
   CREATE VIEW policy_performance_summary AS
   SELECT 
       policy,
       concurrency,
       COUNT(*) AS total_runs,
       ROUND(AVG(hit_rate_percent), 2) AS avg_hit_rate_percent,
       ROUND(AVG(throughput_ops_sec), 2) AS avg_throughput_ops_sec,
       ROUND(AVG(avg_latency_ms), 4) AS avg_latency_ms,
       ROUND(AVG(p95_latency_ms), 4) AS avg_p95_latency_ms,
       COALESCE(SUM(server_evictions), 0) AS total_server_evictions
   FROM benchmarks
   GROUP BY policy, concurrency;
   ```

---

### SQL Triggers

1. **`trg_cache_keys_update_timestamp`** (Auto-timestamping on modification)
   ```sql
   CREATE TRIGGER trg_cache_keys_update_timestamp
   AFTER UPDATE ON cache_keys
   FOR EACH ROW
   WHEN NEW.updated_at = OLD.updated_at OR NEW.updated_at IS NULL
   BEGIN
       UPDATE cache_keys
       SET updated_at = DATETIME('now')
       WHERE key = NEW.key;
   END;
   ```

2. **`trg_validate_stock_price`** (Domain Integrity Constraint)
   ```sql
   CREATE TRIGGER trg_validate_stock_price
   BEFORE INSERT ON stocks
   FOR EACH ROW
   WHEN NEW.close < 0
   BEGIN
       SELECT RAISE(ABORT, 'DBMS Integrity Violation: Stock closing price cannot be negative');
   END;
   ```

---

## 3. Stored Procedure-Like Wrapper Functions (ACID Compliant)

Implemented in `database_manager.py`:
* `sp_batch_insert_stocks(records, chunk_size=500)`: Atomically batch inserts historical stock records with input sanitization and rollback on failure.
* `sp_log_benchmark_run(summary, rows, config)`: Atomically logs benchmark execution runs and foreign-keyed per-operation eviction logs.
* `sp_update_cache_key(key, value, key_type, ttl, byte_size)`: Upserts cache key and fires the update timestamp trigger.
* `sp_query_latest_quotes()`: Abstracted query over the `latest_stock_quotes` SQL View.
* `sp_get_policy_benchmark_comparison()`: Aggregated policy evaluation from `policy_performance_summary` SQL View.

---

## 4. Verification

Run the test suite:

```bash
python verify_dbms_integration.py
```
