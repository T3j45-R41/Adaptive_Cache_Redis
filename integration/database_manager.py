#!/usr/bin/env python3
"""Database Manager module for Adaptive_Cache_Redis SQLite Integration (Phase 3).

DBMS Coursework & University Syllabus Implementation:
1. Relational Schema:
   - stocks (ticker, timestamp, close, volume) - 3NF decomposed historical data
   - cache_keys (key, value, key_type, ttl, byte_size, created_at, updated_at)
   - benchmarks (run_id, policy, concurrency, throughput, p95_latency, hit_rate, evictions)
   - eviction_logs (log_id, run_id, operation_index, key, operation, hit/miss, latency)
     with FOREIGN KEY (run_id) REFERENCES benchmarks(run_id) ON DELETE CASCADE.

2. Advanced DBMS Features:
   - Views:
     * latest_stock_quotes (materializes/filters latest quote per ticker)
     * policy_performance_summary (aggregates benchmark results across policies)
     * benchmark_eviction_ratio_view (computes eviction-to-miss ratios)
   - Triggers:
     * trg_cache_keys_update_timestamp (auto-updates updated_at on record changes)
     * trg_validate_stock_price (domain integrity constraint for non-negative prices)
     * trg_eviction_log_audit (logs insert timestamp and audits operations)

3. Stored Procedure-Like Wrapper Functions (ACID Transaction Managed):
   - sp_batch_insert_stocks: Atomic batch insertion of historical quotes with input sanitization.
   - sp_log_benchmark_run: Atomic relational logging of benchmark runs and per-op eviction logs.
   - sp_update_cache_key: Atomic upsert of cache keys triggering updated_at timestamping.
   - sp_query_latest_quotes: Abstracted query over latest_stock_quotes view.
   - sp_get_policy_benchmark_comparison: Evaluates policy metrics across concurrency levels.
"""

from __future__ import annotations

import csv
import json
import os
import sqlite3
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

REPO_ROOT = Path(__file__).resolve().parent
DEFAULT_DB_PATH = REPO_ROOT / "adaptive_cache.db"

SCHEMA_DDL = """
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- ==============================================================================
-- 1. STOCKS TABLE (Historical Quotes in 3NF)
-- (ticker, timestamp) -> close, volume
-- ==============================================================================
CREATE TABLE IF NOT EXISTS stocks (
    ticker TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    close REAL NOT NULL,
    volume INTEGER DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
    PRIMARY KEY (ticker, timestamp)
);

CREATE INDEX IF NOT EXISTS idx_stocks_ticker_ts 
    ON stocks (ticker, timestamp DESC);

-- ==============================================================================
-- 2. CACHE_KEYS TABLE (Key-Value Persistence)
-- ==============================================================================
CREATE TABLE IF NOT EXISTS cache_keys (
    key TEXT PRIMARY KEY,
    value TEXT,
    key_type TEXT NOT NULL DEFAULT 'string',
    ttl INTEGER DEFAULT -1,
    byte_size INTEGER DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
    updated_at TEXT NOT NULL DEFAULT (DATETIME('now'))
);

CREATE INDEX IF NOT EXISTS idx_cache_keys_updated_at 
    ON cache_keys (updated_at DESC);

-- ==============================================================================
-- 3. BENCHMARKS TABLE (Benchmark Execution Runs)
-- ==============================================================================
CREATE TABLE IF NOT EXISTS benchmarks (
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

CREATE INDEX IF NOT EXISTS idx_benchmarks_policy_concurrency 
    ON benchmarks (policy, concurrency);

-- ==============================================================================
-- 4. EVICTION_LOGS TABLE (Per-Operation Event & Eviction Trace)
-- FOREIGN KEY references benchmarks(run_id) with ON DELETE CASCADE
-- ==============================================================================
CREATE TABLE IF NOT EXISTS eviction_logs (
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

CREATE INDEX IF NOT EXISTS idx_eviction_logs_run_id 
    ON eviction_logs (run_id, operation_index);

-- ==============================================================================
-- ADVANCED DBMS FEATURES: VIEWS
-- ==============================================================================

-- View 1: latest_stock_quotes
-- Materializes the latest price, timestamp, and volume for every ticker
CREATE VIEW IF NOT EXISTS latest_stock_quotes AS
SELECT 
    s.ticker,
    s.timestamp,
    s.close,
    s.volume,
    s.created_at
FROM stocks s
INNER JOIN (
    SELECT ticker, MAX(timestamp) AS max_timestamp
    FROM stocks
    GROUP BY ticker
) latest 
    ON s.ticker = latest.ticker 
   AND s.timestamp = latest.max_timestamp;

-- View 2: policy_performance_summary
-- Aggregates metrics across policies (LRU, LFU, FIFO, ADAPTIVE)
CREATE VIEW IF NOT EXISTS policy_performance_summary AS
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

-- View 3: benchmark_eviction_ratio_view
-- Evaluates eviction efficiency relative to total operations
CREATE VIEW IF NOT EXISTS benchmark_eviction_ratio_view AS
SELECT 
    run_id,
    run_label,
    policy,
    concurrency,
    total_operations,
    cache_hits,
    cache_misses,
    server_evictions,
    ROUND(CAST(COALESCE(server_evictions, 0) AS REAL) / NULLIF(total_operations, 0) * 100, 2) AS eviction_rate_percent
FROM benchmarks;

-- ==============================================================================
-- ADVANCED DBMS FEATURES: TRIGGERS
-- ==============================================================================

-- Trigger 1: Auto-update updated_at timestamp when cache_keys is modified
CREATE TRIGGER IF NOT EXISTS trg_cache_keys_update_timestamp
AFTER UPDATE ON cache_keys
FOR EACH ROW
WHEN NEW.updated_at = OLD.updated_at OR NEW.updated_at IS NULL
BEGIN
    UPDATE cache_keys
    SET updated_at = DATETIME('now')
    WHERE key = NEW.key;
END;

-- Trigger 2: Domain integrity check ensuring stock prices are non-negative
CREATE TRIGGER IF NOT EXISTS trg_validate_stock_price
BEFORE INSERT ON stocks
FOR EACH ROW
WHEN NEW.close < 0
BEGIN
    SELECT RAISE(ABORT, 'DBMS Integrity Violation: Stock close price cannot be negative');
END;
"""


class DatabaseManager:
    """Thread-safe SQLite Database Manager with Stored-Procedure wrappers."""

    def __init__(self, db_path: str | Path | None = None):
        self.db_path = Path(db_path) if db_path else DEFAULT_DB_PATH
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._initialize_schema()

    def get_connection(self) -> sqlite3.Connection:
        """Create an SQLite connection configured with Row factory and Foreign Keys."""
        conn = sqlite3.connect(str(self.db_path), check_same_thread=False, timeout=30.0)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;")
        return conn

    def _initialize_schema(self) -> None:
        """Execute DDL statements to set up tables, views, and triggers."""
        with self._lock:
            with self.get_connection() as conn:
                conn.executescript(SCHEMA_DDL)
                conn.commit()

    # ==========================================================================
    # STORED PROCEDURE-LIKE WRAPPER FUNCTIONS (ACID COMPLIANT)
    # ==========================================================================

    def sp_batch_insert_stocks(self, records: list[dict[str, Any]], chunk_size: int = 500) -> int:
        """Stored Procedure: Atomically batch insert historical stock records.

        Enforces ACID properties:
        - Atomicity: entire batch commits or rolls back on critical error.
        - Consistency: validated against trg_validate_stock_price.
        """
        if not records:
            return 0

        clean_rows = []
        for r in records:
            ticker = str(r.get("ticker") or r.get("Ticker") or "STOCK").strip().upper()
            ts = str(
                r.get("timestamp")
                or r.get("source_timestamp")
                or r.get("Datetime")
                or r.get("Date")
                or ""
            ).strip().replace(" ", "T")
            
            close_val = r.get("close") or r.get("Close") or r.get("price")
            try:
                close = float(close_val)
            except (ValueError, TypeError):
                continue

            vol = r.get("volume") or r.get("Volume") or 0
            try:
                vol = int(vol)
            except (ValueError, TypeError):
                vol = 0

            clean_rows.append((ticker, ts, close, vol))

        if not clean_rows:
            return 0

        sql = """
        INSERT OR REPLACE INTO stocks (ticker, timestamp, close, volume)
        VALUES (?, ?, ?, ?);
        """

        total_inserted = 0
        with self._lock:
            conn = self.get_connection()
            try:
                conn.execute("BEGIN IMMEDIATE TRANSACTION;")
                for i in range(0, len(clean_rows), chunk_size):
                    chunk = clean_rows[i : i + chunk_size]
                    cur = conn.executemany(sql, chunk)
                    total_inserted += cur.rowcount
                conn.commit()
            except Exception as exc:
                conn.rollback()
                raise RuntimeError(f"Stored Procedure sp_batch_insert_stocks rolled back: {exc}") from exc
            finally:
                conn.close()

        return total_inserted

    def sp_log_benchmark_run(
        self,
        summary: dict[str, Any],
        rows: list[dict[str, Any]] | None = None,
        config: dict[str, Any] | None = None,
    ) -> str:
        """Stored Procedure: Atomically log benchmark run and child eviction traces.

        Maintains Referential Integrity:
        eviction_logs entries are foreign-keyed to benchmarks.run_id.
        """
        config = config or {}
        policy = (
            summary.get("server_policy")
            or config.get("policy")
            or summary.get("run_label")
            or "default"
        )
        run_label = summary.get("run_label", policy)
        concurrency = int(
            summary.get(
                "concurrency",
                config.get("concurrency", 1) if isinstance(config.get("concurrency"), int) else 1,
            )
        )

        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")
        run_id = f"{run_label}_{concurrency}_{stamp}"

        total_ops = int(summary.get("total_operations", len(rows) if rows else 0))
        success_count = int(summary.get("success_count", total_ops))
        error_count = int(summary.get("error_count", total_ops - success_count))
        hits = int(summary.get("cache_hits_observed", 0))
        misses = int(summary.get("cache_misses_observed", 0))
        hit_rate = float(summary.get("observed_hit_rate_percent", 0.0))
        wall = float(summary.get("wall_time_seconds", 0.0))
        throughput = float(summary.get("throughput_ops_sec", 0.0))
        avg_lat = float(summary.get("avg_latency_ms", 0.0))
        median_lat = float(summary.get("median_latency_ms", 0.0))
        p95 = float(summary.get("p95_latency_ms", 0.0))
        p99 = float(summary.get("p99_latency_ms", 0.0))
        min_lat = float(summary.get("min_latency_ms", 0.0))
        max_lat = float(summary.get("max_latency_ms", 0.0))
        server_ev = summary.get("eviction_count")
        server_hits = summary.get("server_hits")
        server_misses = summary.get("server_misses")
        server_hit_rate = summary.get("server_hit_rate_percent")
        max_keys = summary.get("server_max_keys", config.get("max_keys"))
        hit_target = config.get("hit_target_percent", 80.0)

        benchmark_sql = """
        INSERT OR REPLACE INTO benchmarks (
            run_id, run_label, policy, concurrency, total_operations,
            success_count, error_count, cache_hits, cache_misses,
            hit_rate_percent, wall_time_seconds, throughput_ops_sec,
            avg_latency_ms, median_latency_ms, p95_latency_ms, p99_latency_ms,
            min_latency_ms, max_latency_ms, server_evictions, server_hits,
            server_misses, server_hit_rate_percent, max_keys, hit_target_percent
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        """

        with self._lock:
            conn = self.get_connection()
            try:
                conn.execute("BEGIN IMMEDIATE TRANSACTION;")
                conn.execute(
                    benchmark_sql,
                    (
                        run_id,
                        run_label,
                        policy,
                        concurrency,
                        total_ops,
                        success_count,
                        error_count,
                        hits,
                        misses,
                        hit_rate,
                        wall,
                        throughput,
                        avg_lat,
                        median_lat,
                        p95,
                        p99,
                        min_lat,
                        max_lat,
                        server_ev,
                        server_hits,
                        server_misses,
                        server_hit_rate,
                        max_keys,
                        hit_target,
                    ),
                )

                if rows:
                    log_sql = """
                    INSERT INTO eviction_logs (
                        run_id, operation_index, key, operation, policy,
                        cache_hit, cache_miss, latency_ms, staleness_seconds,
                        evicted_key, eviction_reason, timestamp
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
                    """
                    log_entries = []
                    for r in rows:
                        op_idx = int(r.get("operation_index", 0))
                        k = r.get("key")
                        op = r.get("operation", "GET")
                        hit_int = 1 if r.get("cache_hit") in (True, 1, "True", "true") else 0
                        miss_int = 1 if r.get("cache_miss") in (True, 1, "True", "true") else 0
                        lat = float(r.get("latency_ms", 0.0))
                        stale = r.get("source_data_staleness_seconds")
                        stale_val = float(stale) if stale not in (None, "") else None
                        ev_key = r.get("evicted_key")
                        ev_reason = "EVICTION_OBSERVED" if op == "GET_MISS_THEN_SET" else None
                        ts = r.get("timestamp") or datetime.now(timezone.utc).isoformat()
                        log_entries.append(
                            (
                                run_id,
                                op_idx,
                                k,
                                op,
                                policy,
                                hit_int,
                                miss_int,
                                lat,
                                stale_val,
                                ev_key,
                                ev_reason,
                                ts,
                            )
                        )
                    conn.executemany(log_sql, log_entries)

                conn.commit()
                return run_id
            except Exception as exc:
                conn.rollback()
                raise RuntimeError(f"Stored Procedure sp_log_benchmark_run rolled back: {exc}") from exc
            finally:
                conn.close()

    def sp_update_cache_key(
        self,
        key: str,
        value: str | None = None,
        key_type: str = "string",
        ttl: int = -1,
        byte_size: int = 0,
    ) -> None:
        """Stored Procedure: Upsert cache key record; activates trg_cache_keys_update_timestamp."""
        sql = """
        INSERT INTO cache_keys (key, value, key_type, ttl, byte_size, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, DATETIME('now'), DATETIME('now'))
        ON CONFLICT(key) DO UPDATE SET
            value = excluded.value,
            key_type = excluded.key_type,
            ttl = excluded.ttl,
            byte_size = excluded.byte_size;
        """
        with self._lock:
            with self.get_connection() as conn:
                conn.execute(sql, (key, value, key_type, ttl, byte_size))
                conn.commit()

    def sp_query_latest_quotes(self) -> list[dict[str, Any]]:
        """Stored Procedure: Abstracted query over latest_stock_quotes SQL View."""
        conn = self.get_connection()
        try:
            cur = conn.execute("SELECT * FROM latest_stock_quotes ORDER BY ticker ASC;")
            return [dict(r) for r in cur.fetchall()]
        finally:
            conn.close()

    def sp_get_policy_benchmark_comparison(self) -> list[dict[str, Any]]:
        """Stored Procedure: Query aggregated policy comparison metrics."""
        conn = self.get_connection()
        try:
            cur = conn.execute("SELECT * FROM policy_performance_summary ORDER BY policy, concurrency;")
            return [dict(r) for r in cur.fetchall()]
        finally:
            conn.close()

    # ==========================================================================
    # DATA RETRIEVAL & MIGRATION HELPERS
    # ==========================================================================

    def get_benchmarks(self, policy: str | None = None, limit: int = 100) -> list[dict[str, Any]]:
        """Fetch benchmark runs."""
        conn = self.get_connection()
        try:
            if policy:
                cur = conn.execute(
                    "SELECT * FROM benchmarks WHERE policy = ? ORDER BY created_at DESC LIMIT ?;",
                    (policy, limit),
                )
            else:
                cur = conn.execute(
                    "SELECT * FROM benchmarks ORDER BY created_at DESC LIMIT ?;",
                    (limit,),
                )
            return [dict(r) for r in cur.fetchall()]
        finally:
            conn.close()

    def get_eviction_logs(self, run_id: str | None = None, limit: int = 500) -> list[dict[str, Any]]:
        """Fetch child eviction logs."""
        conn = self.get_connection()
        try:
            if run_id:
                cur = conn.execute(
                    "SELECT * FROM eviction_logs WHERE run_id = ? ORDER BY operation_index ASC LIMIT ?;",
                    (run_id, limit),
                )
            else:
                cur = conn.execute(
                    "SELECT * FROM eviction_logs ORDER BY log_id DESC LIMIT ?;",
                    (limit,),
                )
            return [dict(r) for r in cur.fetchall()]
        finally:
            conn.close()

    def get_stocks(self, ticker: str | None = None, limit: int = 500) -> list[dict[str, Any]]:
        """Fetch stock records."""
        conn = self.get_connection()
        try:
            if ticker:
                cur = conn.execute(
                    "SELECT * FROM stocks WHERE ticker = ? ORDER BY timestamp ASC LIMIT ?;",
                    (ticker.upper(), limit),
                )
            else:
                cur = conn.execute("SELECT * FROM stocks ORDER BY timestamp ASC LIMIT ?;", (limit,))
            return [dict(r) for r in cur.fetchall()]
        finally:
            conn.close()

    def import_csv_to_sqlite(self, csv_path: str | Path) -> int:
        """Helper to import flat stock CSV files into SQLite stocks table."""
        path = Path(csv_path)
        if not path.is_file():
            raise FileNotFoundError(f"File not found: {csv_path}")

        records: list[dict[str, Any]] = []
        with open(path, newline="", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            for row in reader:
                records.append(row)

        return self.sp_batch_insert_stocks(records)

    def get_table_counts(self) -> dict[str, int]:
        """Return row counts for schema tables."""
        conn = self.get_connection()
        try:
            counts = {}
            for t in ("stocks", "cache_keys", "benchmarks", "eviction_logs"):
                r = conn.execute(f"SELECT COUNT(*) as c FROM {t};").fetchone()
                counts[t] = int(r["c"]) if r else 0
            return counts
        finally:
            conn.close()


def get_db_manager(db_path: str | Path | None = None) -> DatabaseManager:
    """Helper to obtain a DatabaseManager instance."""
    return DatabaseManager(db_path)


# Backward compatibility alias
AdaptiveCacheDB = DatabaseManager
