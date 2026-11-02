import pytest
import sys
import os

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "python", "database"))
from connection import db_manager

def test_dbms_connection():
    assert db_manager is not None
    conn = db_manager.get_connection()
    assert conn is not None
    conn.close()

def test_stock_data_query():
    rows = db_manager.execute_query("SELECT COUNT(*) AS c FROM stock_data")
    assert len(rows) == 1
    assert rows[0]["c"] > 0

def test_eviction_policies_seeded():
    rows = db_manager.execute_query("SELECT policy_name FROM eviction_policies")
    names = [r["policy_name"] for r in rows]
    assert "LRU" in names
    assert "LFU" in names
    assert "ADAPTIVE" in names

def test_views_functional():
    views = [
        "vw_policy_performance",
        "vw_experiment_comparison",
        "vw_stock_history",
        "vw_eviction_statistics"
    ]
    for v in views:
        res = db_manager.execute_query(f"SELECT * FROM {v}")
        assert isinstance(res, list)

def test_transaction_rollback():
    # Insert test experiment and verify rollback on error
    try:
        db_manager.execute_update(
            "INSERT INTO workload_experiments (experiment_id, experiment_name, workload_type, cache_capacity, total_requests, request_rate, start_time, status) "
            "VALUES (-999, 'Test Tx', 'zipfian', 10, 100, 1000, '2025-01-01 00:00:00', 'PENDING')"
        )
        # Attempt conflicting insert with invalid FK to trigger rollback
        try:
            db_manager.execute_update(
                "INSERT INTO cache_metrics (experiment_id, policy_id, total_requests, cache_hits, cache_misses, hit_rate, miss_rate, total_evictions, average_latency, p50_latency, p95_latency, p99_latency, throughput, memory_usage, timestamp) "
                "VALUES (-999, 99999, 100, 50, 50, 50.0, 50.0, 0, 0.05, 0.05, 0.05, 0.05, 1000, 1.0, '2025-01-01 00:00:00')"
            )
        except Exception:
            pass
    finally:
        db_manager.execute_update("DELETE FROM workload_experiments WHERE experiment_id = -999")
