-- =============================================================================
-- Adaptive Cache DBMS Stored Procedures
-- Automated analytics, comparative summaries, and transactional execution
-- =============================================================================

USE adaptive_cache_db;

DELIMITER $$

-- -----------------------------------------------------------------------------
-- Procedure 1: sp_get_experiment_summary
-- Analyzes an experiment and populates/retrieves executive summary
-- -----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_get_experiment_summary$$
CREATE PROCEDURE sp_get_experiment_summary(IN p_exp_id INT)
BEGIN
    DECLARE v_best_policy VARCHAR(32);
    DECLARE v_highest_hit DECIMAL(6, 3);
    DECLARE v_lowest_lat DECIMAL(10, 4);
    DECLARE v_highest_tp DECIMAL(10, 2);
    DECLARE v_lowest_stale DECIMAL(10, 4);

    -- Find policy with the highest hit rate
    SELECT p.policy_name, b.hit_rate
    INTO v_best_policy, v_highest_hit
    FROM benchmark_results b
    JOIN eviction_policies p ON b.policy_id = p.policy_id
    WHERE b.experiment_id = p_exp_id
    ORDER BY b.hit_rate DESC
    LIMIT 1;

    -- Aggregate other optimal metrics
    SELECT 
        MIN(average_latency),
        MAX(throughput),
        MIN(staleness)
    INTO v_lowest_lat, v_highest_tp, v_lowest_stale
    FROM benchmark_results
    WHERE experiment_id = p_exp_id;

    -- Insert or replace into experiment_summary
    INSERT INTO experiment_summary (
        experiment_id, best_policy, highest_hit_rate, 
        lowest_latency, highest_throughput, lowest_staleness, generated_at
    ) VALUES (
        p_exp_id, IFNULL(v_best_policy, 'N/A'), IFNULL(v_highest_hit, 0.0),
        IFNULL(v_lowest_lat, 0.0), IFNULL(v_highest_tp, 0.0), IFNULL(v_lowest_stale, 0.0),
        NOW()
    )
    ON DUPLICATE KEY UPDATE
        best_policy = VALUES(best_policy),
        highest_hit_rate = VALUES(highest_hit_rate),
        lowest_latency = VALUES(lowest_latency),
        highest_throughput = VALUES(highest_throughput),
        lowest_staleness = VALUES(lowest_staleness),
        generated_at = NOW();

    -- Return the summary record
    SELECT * FROM experiment_summary WHERE experiment_id = p_exp_id;
END$$

-- -----------------------------------------------------------------------------
-- Procedure 2: sp_compare_eviction_policies
-- Returns comparative performance matrix for a given experiment
-- -----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_compare_eviction_policies$$
CREATE PROCEDURE sp_compare_eviction_policies(IN p_exp_id INT)
BEGIN
    SELECT 
        e.experiment_id,
        e.experiment_name,
        e.workload_type,
        e.cache_capacity,
        p.policy_name,
        p.is_adaptive,
        b.hit_rate,
        (100.0 - b.hit_rate) AS miss_rate,
        b.average_latency,
        b.p95_latency,
        b.throughput,
        b.staleness,
        b.execution_time
    FROM benchmark_results b
    JOIN eviction_policies p ON b.policy_id = p.policy_id
    JOIN workload_experiments e ON b.experiment_id = e.experiment_id
    WHERE b.experiment_id = p_exp_id
    ORDER BY b.hit_rate DESC;
END$$

-- -----------------------------------------------------------------------------
-- Procedure 3: sp_get_policy_metrics
-- Aggregates metrics for a specific policy across all experiments
-- -----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_get_policy_metrics$$
CREATE PROCEDURE sp_get_policy_metrics(IN p_policy_name VARCHAR(32))
BEGIN
    SELECT 
        p.policy_name,
        COUNT(b.result_id) AS total_benchmarks,
        ROUND(AVG(b.hit_rate), 2) AS avg_hit_rate,
        ROUND(AVG(b.average_latency), 4) AS avg_latency,
        ROUND(AVG(b.throughput), 2) AS avg_throughput,
        ROUND(AVG(b.staleness), 4) AS avg_staleness
    FROM eviction_policies p
    LEFT JOIN benchmark_results b ON p.policy_id = b.policy_id
    WHERE p.policy_name = p_policy_name
    GROUP BY p.policy_name;
END$$

-- -----------------------------------------------------------------------------
-- Procedure 4: sp_record_benchmark_transaction
-- Demonstrates transactional consistency with COMMIT and ROLLBACK
-- -----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_record_benchmark_transaction$$
CREATE PROCEDURE sp_record_benchmark_transaction(
    IN p_exp_id INT,
    IN p_policy_name VARCHAR(32),
    IN p_workload VARCHAR(64),
    IN p_capacity INT,
    IN p_hit_rate DECIMAL(6, 3),
    IN p_avg_lat DECIMAL(10, 4),
    IN p_p95_lat DECIMAL(10, 4),
    IN p_throughput DECIMAL(10, 2),
    IN p_staleness DECIMAL(10, 4),
    IN p_exec_time DECIMAL(10, 2),
    OUT p_status VARCHAR(64)
)
proc_body: BEGIN
    DECLARE v_policy_id INT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_status = 'TRANSACTION_FAILED_ROLLEDBACK';
    END;

    START TRANSACTION;

    SELECT policy_id INTO v_policy_id
    FROM eviction_policies
    WHERE policy_name = p_policy_name
    LIMIT 1;

    IF v_policy_id IS NULL THEN
        ROLLBACK;
        SET p_status = 'ERROR_UNKNOWN_POLICY';
        LEAVE proc_body;
    END IF;

    -- Insert or update benchmark result
    INSERT INTO benchmark_results (
        experiment_id, policy_id, workload_type, cache_capacity,
        hit_rate, average_latency, p95_latency, throughput, staleness, execution_time, created_at
    ) VALUES (
        p_exp_id, v_policy_id, p_workload, p_capacity,
        p_hit_rate, p_avg_lat, p_p95_lat, p_throughput, p_staleness, p_exec_time, NOW()
    )
    ON DUPLICATE KEY UPDATE
        hit_rate = VALUES(hit_rate),
        average_latency = VALUES(average_latency),
        p95_latency = VALUES(p95_latency),
        throughput = VALUES(throughput),
        staleness = VALUES(staleness),
        execution_time = VALUES(execution_time);

    COMMIT;
    SET p_status = 'TRANSACTION_COMMITTED_SUCCESSFULLY';
END proc_body$$

DELIMITER ;
