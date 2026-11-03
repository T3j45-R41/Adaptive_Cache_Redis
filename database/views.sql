-- =============================================================================
-- Adaptive Cache DBMS Views
-- High-performance aggregated analytical views
-- =============================================================================

USE adaptive_cache_db;

-- -----------------------------------------------------------------------------
-- View 1: vw_policy_performance
-- Overall historical performance aggregated per eviction policy
-- -----------------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_policy_performance AS
SELECT 
    p.policy_id,
    p.policy_name,
    p.is_adaptive,
    COUNT(b.result_id) AS total_runs,
    ROUND(AVG(b.hit_rate), 2) AS avg_hit_rate,
    ROUND(MIN(b.hit_rate), 2) AS min_hit_rate,
    ROUND(MAX(b.hit_rate), 2) AS max_hit_rate,
    ROUND(AVG(b.average_latency), 4) AS avg_latency_ms,
    ROUND(AVG(b.p95_latency), 4) AS avg_p95_latency_ms,
    ROUND(AVG(b.throughput), 2) AS avg_throughput_rps,
    ROUND(AVG(b.staleness), 4) AS avg_staleness_sec
FROM eviction_policies p
LEFT JOIN benchmark_results b ON p.policy_id = b.policy_id
GROUP BY p.policy_id, p.policy_name, p.is_adaptive;

-- -----------------------------------------------------------------------------
-- View 2: vw_experiment_comparison
-- Matrix view comparing all policies within each experiment run
-- -----------------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_experiment_comparison AS
SELECT 
    e.experiment_id,
    e.experiment_name,
    e.workload_type,
    e.cache_capacity,
    e.total_requests,
    p.policy_name,
    p.is_adaptive,
    b.hit_rate,
    b.average_latency,
    b.p95_latency,
    b.throughput,
    b.staleness,
    b.execution_time,
    b.created_at
FROM workload_experiments e
INNER JOIN benchmark_results b ON e.experiment_id = b.experiment_id
INNER JOIN eviction_policies p ON b.policy_id = p.policy_id
ORDER BY e.experiment_id DESC, b.hit_rate DESC;

-- -----------------------------------------------------------------------------
-- View 3: vw_stock_history
-- Summary statistics of ingested stock ticker records
-- -----------------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_stock_history AS
SELECT 
    symbol,
    COUNT(id) AS quote_count,
    ROUND(MIN(price), 2) AS min_price,
    ROUND(MAX(price), 2) AS max_price,
    ROUND(AVG(price), 2) AS avg_price,
    SUM(volume) AS total_volume,
    MIN(market_timestamp) AS earliest_record,
    MAX(market_timestamp) AS latest_record
FROM stock_data
GROUP BY symbol;

-- -----------------------------------------------------------------------------
-- View 4: vw_eviction_statistics
-- Eviction frequency and reason distribution per policy
-- -----------------------------------------------------------------------------
CREATE OR REPLACE VIEW vw_eviction_statistics AS
SELECT 
    p.policy_name,
    l.eviction_reason,
    COUNT(l.eviction_id) AS eviction_count,
    ROUND(AVG(l.access_frequency), 2) AS avg_key_freq_at_eviction,
    MIN(l.eviction_timestamp) AS first_eviction_time,
    MAX(l.eviction_timestamp) AS last_eviction_time
FROM eviction_logs l
INNER JOIN eviction_policies p ON l.policy_id = p.policy_id
GROUP BY p.policy_name, l.eviction_reason;
