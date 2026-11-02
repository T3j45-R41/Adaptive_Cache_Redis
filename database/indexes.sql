-- =============================================================================
-- Adaptive Cache DBMS Indexes & Query Optimization
-- =============================================================================

USE adaptive_cache_db;

-- Composite index on stock_data: speeds up symbol lookups by timestamp
CREATE INDEX idx_stock_symbol_time 
    ON stock_data(symbol, market_timestamp);

-- Timestamp index for market time range queries
CREATE INDEX idx_stock_market_time 
    ON stock_data(market_timestamp);

-- Composite index on workload_requests: fast sequence retrieval
CREATE INDEX idx_req_exp_seq 
    ON workload_requests(experiment_id, sequence_num);

-- Composite index on cache_metrics: instant retrieval of experiment's policy metric
CREATE INDEX idx_metric_exp_policy 
    ON cache_metrics(experiment_id, policy_id);

-- Composite index on eviction_logs: query evictions by experiment and timestamp
CREATE INDEX idx_evict_exp_time 
    ON eviction_logs(experiment_id, eviction_timestamp);

-- Composite index on adaptive_policy_metrics: fast time-series weight extraction
CREATE INDEX idx_adaptive_exp_time 
    ON adaptive_policy_metrics(experiment_id, timestamp);

-- Status and start_time index for filtering experiments
CREATE INDEX idx_exp_status_time 
    ON workload_experiments(status, start_time);
