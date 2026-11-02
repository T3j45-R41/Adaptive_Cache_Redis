-- =============================================================================
-- Adaptive Cache Eviction for Real-Time Stock Quote Serving
-- Relational Database Schema: MySQL 8.x
-- Database Name: adaptive_cache_db
-- =============================================================================

CREATE DATABASE IF NOT EXISTS adaptive_cache_db
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE adaptive_cache_db;

-- Drop dependent tables in reverse order if re-creating
DROP TABLE IF EXISTS experiment_summary;
DROP TABLE IF EXISTS benchmark_results;
DROP TABLE IF EXISTS adaptive_policy_metrics;
DROP TABLE IF EXISTS eviction_logs;
DROP TABLE IF EXISTS cache_metrics;
DROP TABLE IF EXISTS workload_requests;
DROP TABLE IF EXISTS workload_experiments;
DROP TABLE IF EXISTS eviction_policies;
DROP TABLE IF EXISTS stock_data;
DROP TABLE IF EXISTS system_config;
DROP TABLE IF EXISTS experiment_audit_log;

-- -----------------------------------------------------------------------------
-- 1. Table: stock_data
-- Normalized historical stock quotes ingested via yfinance
-- -----------------------------------------------------------------------------
CREATE TABLE stock_data (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    symbol VARCHAR(16) NOT NULL,
    price DECIMAL(12, 4) NOT NULL,
    open_price DECIMAL(12, 4) NOT NULL,
    high_price DECIMAL(12, 4) NOT NULL,
    low_price DECIMAL(12, 4) NOT NULL,
    volume BIGINT NOT NULL,
    market_timestamp DATETIME NOT NULL,
    inserted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_stock_price CHECK (price >= 0),
    CONSTRAINT chk_stock_high_low CHECK (high_price >= low_price),
    CONSTRAINT chk_stock_volume CHECK (volume >= 0)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 2. Table: eviction_policies
-- Master registry of traditional and adaptive eviction algorithms
-- -----------------------------------------------------------------------------
CREATE TABLE eviction_policies (
    policy_id INT AUTO_INCREMENT PRIMARY KEY,
    policy_name VARCHAR(32) NOT NULL UNIQUE,
    policy_description TEXT,
    is_adaptive BOOLEAN NOT NULL DEFAULT FALSE
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 3. Table: workload_experiments
-- Metadata and execution parameters for benchmarking runs
-- -----------------------------------------------------------------------------
CREATE TABLE workload_experiments (
    experiment_id INT AUTO_INCREMENT PRIMARY KEY,
    experiment_name VARCHAR(128) NOT NULL,
    workload_type VARCHAR(64) NOT NULL,
    cache_capacity INT NOT NULL,
    total_requests INT NOT NULL,
    request_rate DECIMAL(10, 2) NOT NULL,
    start_time DATETIME NOT NULL,
    end_time DATETIME DEFAULT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'PENDING',
    CONSTRAINT chk_exp_capacity CHECK (cache_capacity > 0),
    CONSTRAINT chk_exp_requests CHECK (total_requests >= 0)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 4. Table: workload_requests
-- Fine-grained trace requests generated for deterministic benchmarking
-- -----------------------------------------------------------------------------
CREATE TABLE workload_requests (
    request_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    sequence_num INT NOT NULL,
    symbol VARCHAR(16) NOT NULL,
    request_type VARCHAR(16) NOT NULL DEFAULT 'GET',
    scheduled_delay_ms INT NOT NULL DEFAULT 0,
    CONSTRAINT fk_req_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 5. Table: cache_metrics
-- Point-in-time and final aggregated performance measurements
-- -----------------------------------------------------------------------------
CREATE TABLE cache_metrics (
    metric_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    policy_id INT NOT NULL,
    total_requests INT NOT NULL,
    cache_hits INT NOT NULL,
    cache_misses INT NOT NULL,
    hit_rate DECIMAL(6, 3) NOT NULL,
    miss_rate DECIMAL(6, 3) NOT NULL,
    total_evictions INT NOT NULL,
    average_latency DECIMAL(10, 4) NOT NULL,
    p50_latency DECIMAL(10, 4) NOT NULL,
    p95_latency DECIMAL(10, 4) NOT NULL,
    p99_latency DECIMAL(10, 4) NOT NULL,
    throughput DECIMAL(10, 2) NOT NULL,
    memory_usage DECIMAL(10, 2) NOT NULL,
    timestamp DATETIME NOT NULL,
    CONSTRAINT fk_metric_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_metric_policy FOREIGN KEY (policy_id)
        REFERENCES eviction_policies(policy_id)
        ON DELETE RESTRICT,
    CONSTRAINT chk_hit_rate CHECK (hit_rate >= 0.0 AND hit_rate <= 100.0),
    CONSTRAINT chk_miss_rate CHECK (miss_rate >= 0.0 AND miss_rate <= 100.0)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 6. Table: eviction_logs
-- Granular log of eviction events for recency/frequency audit
-- -----------------------------------------------------------------------------
CREATE TABLE eviction_logs (
    eviction_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    policy_id INT NOT NULL,
    evicted_key VARCHAR(128) NOT NULL,
    eviction_reason VARCHAR(64) NOT NULL DEFAULT 'capacity_exceeded',
    access_frequency INT DEFAULT 1,
    last_access_time DATETIME DEFAULT NULL,
    eviction_timestamp DATETIME NOT NULL,
    CONSTRAINT fk_evict_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_evict_policy FOREIGN KEY (policy_id)
        REFERENCES eviction_policies(policy_id)
        ON DELETE RESTRICT
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 7. Table: adaptive_policy_metrics
-- Real-time tracking of CACHEUS expert weights, selections, and regret
-- -----------------------------------------------------------------------------
CREATE TABLE adaptive_policy_metrics (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    expert_name VARCHAR(32) NOT NULL,
    expert_weight DECIMAL(8, 6) NOT NULL,
    cumulative_regret DECIMAL(12, 4) NOT NULL,
    selection_count INT NOT NULL DEFAULT 0,
    reward DECIMAL(8, 4) NOT NULL DEFAULT 0.0,
    timestamp DATETIME NOT NULL,
    CONSTRAINT fk_adaptive_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE,
    CONSTRAINT chk_expert_weight CHECK (expert_weight >= 0.0 AND expert_weight <= 1.0)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 8. Table: benchmark_results
-- Standard comparative performance matrix table
-- -----------------------------------------------------------------------------
CREATE TABLE benchmark_results (
    result_id INT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    policy_id INT NOT NULL,
    workload_type VARCHAR(64) NOT NULL,
    cache_capacity INT NOT NULL,
    hit_rate DECIMAL(6, 3) NOT NULL,
    average_latency DECIMAL(10, 4) NOT NULL,
    p95_latency DECIMAL(10, 4) NOT NULL,
    throughput DECIMAL(10, 2) NOT NULL,
    staleness DECIMAL(10, 4) NOT NULL,
    execution_time DECIMAL(10, 2) NOT NULL,
    created_at DATETIME NOT NULL,
    CONSTRAINT fk_bench_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_bench_policy FOREIGN KEY (policy_id)
        REFERENCES eviction_policies(policy_id)
        ON DELETE RESTRICT,
    CONSTRAINT uq_exp_policy UNIQUE (experiment_id, policy_id)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 9. Table: system_config
-- Global key-value operational configuration
-- -----------------------------------------------------------------------------
CREATE TABLE system_config (
    config_id INT AUTO_INCREMENT PRIMARY KEY,
    config_key VARCHAR(64) NOT NULL UNIQUE,
    config_value VARCHAR(256) NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 10. Table: experiment_summary
-- Executive comparison summary computed via stored procedure
-- -----------------------------------------------------------------------------
CREATE TABLE experiment_summary (
    summary_id INT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL UNIQUE,
    best_policy VARCHAR(32) NOT NULL,
    highest_hit_rate DECIMAL(6, 3) NOT NULL,
    lowest_latency DECIMAL(10, 4) NOT NULL,
    highest_throughput DECIMAL(10, 2) NOT NULL,
    lowest_staleness DECIMAL(10, 4) NOT NULL,
    generated_at DATETIME NOT NULL,
    CONSTRAINT fk_sum_experiment FOREIGN KEY (experiment_id)
        REFERENCES workload_experiments(experiment_id)
        ON DELETE CASCADE
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------
-- 11. Table: experiment_audit_log
-- Audit trail populated by database triggers
-- -----------------------------------------------------------------------------
CREATE TABLE experiment_audit_log (
    audit_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    experiment_id INT NOT NULL,
    old_status VARCHAR(32),
    new_status VARCHAR(32),
    action_type VARCHAR(32) NOT NULL,
    changed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
