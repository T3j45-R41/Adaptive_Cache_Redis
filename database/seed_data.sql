-- =============================================================================
-- Adaptive Cache DBMS Seed Data
-- Initial policies and system configuration
-- =============================================================================

USE adaptive_cache_db;

-- Eviction Policies
INSERT INTO eviction_policies (policy_name, policy_description, is_adaptive) VALUES
('LRU', 'Least Recently Used: Discards items that have not been accessed for the longest period.', FALSE),
('LFU', 'Least Frequently Used: Discards items with the lowest access count with recency tie-breaking.', FALSE),
('FIFO', 'First-In First-Out: Evicts items in the strict chronological order of their initial insertion.', FALSE),
('RANDOM', 'Random Replacement: Evicts a randomly selected key uniformly across all active entries.', FALSE),
('ADAPTIVE', 'Adaptive CACHEUS-style: Dynamic regret-minimizing ensemble between recency and frequency experts.', TRUE)
ON DUPLICATE KEY UPDATE 
    policy_description = VALUES(policy_description),
    is_adaptive = VALUES(is_adaptive);

-- System Configuration Defaults
INSERT INTO system_config (config_key, config_value) VALUES
('default_cache_capacity', '1000'),
('default_eviction_policy', 'ADAPTIVE'),
('redis_server_host', '127.0.0.1'),
('redis_server_port', '6379'),
('database_version', '8.0.46'),
('staleness_threshold_seconds', '5.0'),
('learning_rate_eta', '0.05')
ON DUPLICATE KEY UPDATE config_value = VALUES(config_value);
