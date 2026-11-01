-- ==============================================================================
-- Adaptive Cache Redis - Phase 3 SQLite Database Schema
-- DBMS Coursework: Relational Design, Normalization, Views & Triggers
-- ==============================================================================

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- ------------------------------------------------------------------------------
-- 1. STOCKS TABLE (Historical Market Data in 3NF)
-- ------------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS stocks (
    ticker TEXT NOT NULL,
    timestamp TEXT NOT NULL,              -- ISO-8601 formatted timestamp (YYYY-MM-DDTHH:MM:SS)
    close REAL NOT NULL,                  -- Closing stock price
    volume INTEGER DEFAULT 0,             -- Trading volume
    created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
    PRIMARY KEY (ticker, timestamp)
);

CREATE INDEX IF NOT EXISTS idx_stocks_ticker_ts 
    ON stocks (ticker, timestamp DESC);

-- ------------------------------------------------------------------------------
-- 2. CACHE_KEYS TABLE (Key-Value Persistence & Metadata)
-- ------------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cache_keys (
    key TEXT PRIMARY KEY,
    value TEXT,
    key_type TEXT NOT NULL DEFAULT 'string',
    ttl INTEGER DEFAULT -1,              -- TTL in seconds (-1 = no expiry, -2 = expired)
    byte_size INTEGER DEFAULT 0,          -- Memory usage in bytes
    created_at TEXT NOT NULL DEFAULT (DATETIME('now')),
    updated_at TEXT NOT NULL DEFAULT (DATETIME('now'))
);

CREATE INDEX IF NOT EXISTS idx_cache_keys_updated_at 
    ON cache_keys (updated_at DESC);

-- ------------------------------------------------------------------------------
-- 3. BENCHMARKS TABLE (Benchmark Execution Runs & Aggregates)
-- ------------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS benchmarks (
    run_id TEXT PRIMARY KEY,             -- Unique identifier for the benchmark run
    run_label TEXT NOT NULL,             -- Experiment label (e.g. LRU, LFU, FIFO, ADAPTIVE)
    policy TEXT NOT NULL,                -- Eviction policy tested
    concurrency INTEGER NOT NULL,        -- Concurrency level (number of workers)
    total_operations INTEGER NOT NULL,   -- Total operations executed
    success_count INTEGER NOT NULL,      -- Successful operations
    error_count INTEGER NOT NULL,        -- Failed operations
    cache_hits INTEGER NOT NULL,         -- Observed client cache hits
    cache_misses INTEGER NOT NULL,       -- Observed client cache misses
    hit_rate_percent REAL NOT NULL,      -- Hit rate %: hits / (hits + misses) * 100
    wall_time_seconds REAL NOT NULL,     -- Total wall clock duration
    throughput_ops_sec REAL NOT NULL,    -- Operations per second
    avg_latency_ms REAL NOT NULL,        -- Average round-trip latency
    median_latency_ms REAL NOT NULL,     -- Median latency
    p95_latency_ms REAL NOT NULL,        -- 95th percentile latency
    p99_latency_ms REAL NOT NULL,        -- 99th percentile latency
    min_latency_ms REAL,
    max_latency_ms REAL,
    server_evictions INTEGER,            -- Server eviction count reported by INFO
    server_hits INTEGER,                 -- Server-side total hits
    server_misses INTEGER,               -- Server-side total misses
    server_hit_rate_percent REAL,        -- Server-side hit rate %
    max_keys INTEGER,                    -- Cache capacity limit during run
    hit_target_percent REAL,             -- Target known-key percentage
    created_at TEXT NOT NULL DEFAULT (DATETIME('now'))
);

CREATE INDEX IF NOT EXISTS idx_benchmarks_policy_concurrency 
    ON benchmarks (policy, concurrency);

-- ------------------------------------------------------------------------------
-- 4. EVICTION_LOGS TABLE (Per-Operation Benchmark & Eviction Trace)
-- Child table with FOREIGN KEY referencing benchmarks(run_id) ON DELETE CASCADE
-- ------------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS eviction_logs (
    log_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id TEXT NOT NULL,
    operation_index INTEGER NOT NULL,
    key TEXT,
    operation TEXT NOT NULL,             -- GET, GET_MISS_THEN_SET, GET_COLD, EVICT
    policy TEXT,
    cache_hit INTEGER NOT NULL,          -- 1 for hit, 0 for miss
    cache_miss INTEGER NOT NULL,         -- 1 for miss, 0 for hit
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

-- VIEW 1: latest_stock_quotes
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

-- VIEW 2: policy_performance_summary
-- Aggregates benchmark results by eviction policy and concurrency level
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

-- VIEW 3: benchmark_eviction_ratio_view
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

-- TRIGGER 1: trg_cache_keys_update_timestamp
-- Automatically updates updated_at whenever a record in cache_keys is modified
CREATE TRIGGER IF NOT EXISTS trg_cache_keys_update_timestamp
AFTER UPDATE ON cache_keys
FOR EACH ROW
WHEN NEW.updated_at = OLD.updated_at OR NEW.updated_at IS NULL
BEGIN
    UPDATE cache_keys
    SET updated_at = DATETIME('now')
    WHERE key = NEW.key;
END;

-- TRIGGER 2: trg_validate_stock_price
-- Domain Integrity Constraint: aborts insertion if a stock's closing price is negative
CREATE TRIGGER IF NOT EXISTS trg_validate_stock_price
BEFORE INSERT ON stocks
FOR EACH ROW
WHEN NEW.close < 0
BEGIN
    SELECT RAISE(ABORT, 'DBMS Integrity Violation: Stock closing price cannot be negative');
END;
