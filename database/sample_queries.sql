-- =============================================================================
-- Adaptive Cache DBMS Sample Queries & Concept Demonstrations
-- Demonstrates university-level SQL, indexing, joins, and aggregates
-- =============================================================================

USE adaptive_cache_db;

-- 1. INNER JOIN & Aggregate Functions (COUNT, AVG, MIN, MAX)
-- Compare all policies across experiments with aggregated hit rates and latencies
SELECT 
    p.policy_name,
    COUNT(b.result_id) AS total_runs,
    ROUND(AVG(b.hit_rate), 2) AS avg_hit_rate,
    ROUND(MIN(b.average_latency), 4) AS min_latency_ms,
    ROUND(MAX(b.average_latency), 4) AS max_latency_ms,
    ROUND(AVG(b.throughput), 2) AS avg_throughput_rps
FROM benchmark_results b
INNER JOIN eviction_policies p ON b.policy_id = p.policy_id
GROUP BY p.policy_name
ORDER BY avg_hit_rate DESC;

-- 2. LEFT JOIN to show all configured policies, even if no runs exist
SELECT 
    p.policy_id,
    p.policy_name,
    p.is_adaptive,
    IFNULL(COUNT(b.result_id), 0) AS experiment_count
FROM eviction_policies p
LEFT JOIN benchmark_results b ON p.policy_id = b.policy_id
GROUP BY p.policy_id, p.policy_name, p.is_adaptive;

-- 3. GROUP BY and HAVING Clause
-- Filter policies achieving greater than 60% average hit rate
SELECT 
    p.policy_name,
    COUNT(b.result_id) AS runs_count,
    ROUND(AVG(b.hit_rate), 2) AS avg_hit_rate
FROM benchmark_results b
JOIN eviction_policies p ON b.policy_id = p.policy_id
GROUP BY p.policy_name
HAVING avg_hit_rate > 60.0;

-- 4. EXPLAIN Query demonstrating index usage on composite key
EXPLAIN SELECT * FROM stock_data 
WHERE symbol = 'AAPL' 
  AND market_timestamp >= '2025-01-01 00:00:00';

-- 5. EXPLAIN Query demonstrating metric composite index
EXPLAIN SELECT * FROM cache_metrics 
WHERE experiment_id = 1 
  AND policy_id = 5;

-- 6. Query from analytical views
SELECT * FROM vw_policy_performance;
SELECT * FROM vw_experiment_comparison LIMIT 10;
SELECT * FROM vw_stock_history;
SELECT * FROM vw_eviction_statistics;

-- 7. Call Stored Procedures
CALL sp_get_policy_metrics('ADAPTIVE');
CALL sp_compare_eviction_policies(1);
CALL sp_get_experiment_summary(1);

-- 8. Transaction Demonstration with COMMIT
START TRANSACTION;
UPDATE workload_experiments 
SET status = 'COMPLETED', end_time = NOW() 
WHERE experiment_id = 1;
COMMIT;

-- 9. Check Audit Log populated by Triggers
SELECT * FROM experiment_audit_log ORDER BY changed_at DESC LIMIT 10;
