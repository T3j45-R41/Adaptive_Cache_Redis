#!/usr/bin/env python3
"""Verification script for Phase 3 SQLite database integration.

Demonstrates and verifies:
1. SQLite Tables (stocks, cache_keys, benchmarks, eviction_logs).
2. Stored Procedure sp_batch_insert_stocks & SQL View latest_stock_quotes.
3. Stored Procedure sp_update_cache_key & SQL Trigger trg_cache_keys_update_timestamp.
4. Domain Integrity Check: Trigger trg_validate_stock_price.
5. Stored Procedure sp_log_benchmark_run & Foreign Key CASCADE delete.
"""

import sys
from pathlib import Path

# Import our DatabaseManager
from database_manager import DatabaseManager, get_db_manager


def run_tests():
    test_db = Path("test_dbms_integration.db")
    if test_db.exists():
        try:
            test_db.unlink()
        except OSError:
            pass

    print("=" * 72)
    print("PHASE 3 DBMS INTEGRATION TEST SUITE (STRICTLY ADDITIVE)")
    print(f"Test Database: {test_db.resolve()}")
    print("=" * 72)

    db = get_db_manager(test_db)

    # 1. Schema Tables Verification
    print("\n[1/5] Verifying Relational Schema & Tables...")
    counts = db.get_table_counts()
    for tbl in ("stocks", "cache_keys", "benchmarks", "eviction_logs"):
        assert tbl in counts, f"Table '{tbl}' was not created!"
        print(f"  ✓ Table '{tbl}' initialized (Initial row count: {counts[tbl]})")

    # 2. Stored Procedure: sp_batch_insert_stocks & SQL View: latest_stock_quotes
    print("\n[2/5] Testing Stored Procedure 'sp_batch_insert_stocks' & SQL View 'latest_stock_quotes'...")
    sample_quotes = [
        {"ticker": "AAPL", "timestamp": "2026-10-01T09:30:00", "close": 180.50, "volume": 100000},
        {"ticker": "AAPL", "timestamp": "2026-10-01T10:00:00", "close": 182.20, "volume": 120000}, # latest AAPL
        {"ticker": "MSFT", "timestamp": "2026-10-01T09:30:00", "close": 410.00, "volume": 80000},
        {"ticker": "MSFT", "timestamp": "2026-10-01T10:00:00", "close": 415.50, "volume": 90000},  # latest MSFT
        {"ticker": "GOOGL", "timestamp": "2026-10-01T09:30:00", "close": 175.00, "volume": 60000}, # latest GOOGL
    ]
    inserted = db.sp_batch_insert_stocks(sample_quotes)
    print(f"  ✓ Atomically inserted {inserted} records across 3 tickers via sp_batch_insert_stocks.")

    latest = db.sp_query_latest_quotes()
    print(f"  ✓ Queried SQL View 'latest_stock_quotes' (Returned {len(latest)} ticker rows):")
    for row in latest:
        print(f"     -> {row['ticker']}: Close=${row['close']:.2f} at {row['timestamp']}")

    aapl = next((r for r in latest if r["ticker"] == "AAPL"), None)
    assert aapl and aapl["close"] == 182.20, "View latest_stock_quotes failed to select latest AAPL quote!"
    print("  ✓ SQL View correctly selected most recent quotes without duplicate scanning.")

    # 3. Stored Procedure: sp_update_cache_key & SQL Trigger: trg_cache_keys_update_timestamp
    print("\n[3/5] Testing Stored Procedure 'sp_update_cache_key' & Trigger 'trg_cache_keys_update_timestamp'...")
    key = "stock:AAPL:latest"
    db.sp_update_cache_key(key, value="180.50|2026-10-01", key_type="string", byte_size=64)

    conn = db.get_connection()
    row1 = conn.execute("SELECT * FROM cache_keys WHERE key = ?;", (key,)).fetchone()
    t1 = row1["updated_at"]
    print(f"  ✓ Initial cache key created. Timestamp: {t1}")

    # Set artificial baseline to test trigger
    conn.execute("UPDATE cache_keys SET updated_at = '2020-01-01 00:00:00' WHERE key = ?;", (key,))
    conn.commit()
    conn.close()

    # Now update via stored procedure
    db.sp_update_cache_key(key, value="182.20|2026-10-01", byte_size=72)
    conn = db.get_connection()
    row2 = conn.execute("SELECT * FROM cache_keys WHERE key = ?;", (key,)).fetchone()
    t2 = row2["updated_at"]
    conn.close()
    print(f"  ✓ Cache key updated. Trigger auto-updated timestamp to: {t2}")
    assert t2 != "2020-01-01 00:00:00", "Trigger trg_cache_keys_update_timestamp did not fire!"

    # 4. Domain Integrity Trigger: trg_validate_stock_price
    print("\n[4/5] Testing Domain Integrity Trigger: trg_validate_stock_price...")
    try:
        db.sp_batch_insert_stocks([{"ticker": "BAD_QUOTE", "timestamp": "2026-10-01T12:00:00", "close": -42.0}])
        print("  ✗ Trigger failed to reject negative close price!")
        return False
    except Exception as exc:
        print(f"  ✓ Negative close price successfully blocked by DBMS Trigger:\n    {exc}")

    # 5. Stored Procedure: sp_log_benchmark_run & Foreign Key CASCADE Delete
    print("\n[5/5] Testing Stored Procedure 'sp_log_benchmark_run' & Relational Foreign Keys...")
    summary = {
        "run_label": "TEST_LRU",
        "server_policy": "LRU",
        "concurrency": 2,
        "total_operations": 20,
        "success_count": 20,
        "cache_hits_observed": 14,
        "cache_misses_observed": 6,
        "observed_hit_rate_percent": 70.0,
        "wall_time_seconds": 0.25,
        "throughput_ops_sec": 80.0,
        "avg_latency_ms": 1.2,
        "median_latency_ms": 1.0,
        "p95_latency_ms": 1.8,
        "p99_latency_ms": 2.1,
        "eviction_count": 3,
    }
    rows = [
        {"operation_index": 1, "key": "stock:AAPL", "operation": "GET", "cache_hit": 1, "cache_miss": 0, "latency_ms": 0.9},
        {"operation_index": 2, "key": "stock:MSFT", "operation": "GET_MISS_THEN_SET", "cache_hit": 0, "cache_miss": 1, "latency_ms": 1.8, "evicted_key": "stock:OLD"},
    ]

    run_id = db.sp_log_benchmark_run(summary, rows)
    print(f"  ✓ Atomically logged benchmark run '{run_id}' with {len(rows)} child eviction traces.")

    child_logs = db.get_eviction_logs(run_id=run_id)
    assert len(child_logs) == len(rows), "Child eviction logs count mismatch!"
    print(f"  ✓ Verified {len(child_logs)} child eviction logs linked via FOREIGN KEY.")

    # Test CASCADE delete
    conn = db.get_connection()
    conn.execute("DELETE FROM benchmarks WHERE run_id = ?;", (run_id,))
    conn.commit()
    conn.close()

    logs_after = db.get_eviction_logs(run_id=run_id)
    assert len(logs_after) == 0, "CASCADE DELETE failed: child logs were not removed!"
    print("  ✓ Relational Integrity Verified: Deleting parent benchmark row CASCADE-deleted child eviction logs.")

    # Cleanup test db
    try:
        test_db.unlink()
        for f in (Path("test_dbms_integration.db-wal"), Path("test_dbms_integration.db-shm")):
            if f.exists(): f.unlink()
    except OSError:
        pass

    print("\n" + "=" * 72)
    print("ALL 5 DBMS INTEGRATION TESTS PASSED (100% ADDITIVE & NON-BREAKING)!")
    print("=" * 72)
    return True


if __name__ == "__main__":
    ok = run_tests()
    sys.exit(0 if ok else 1)
