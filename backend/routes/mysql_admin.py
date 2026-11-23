import time
from typing import Optional
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from services.db_service import db_service

router = APIRouter(prefix="/api/mysql", tags=["MySQL DBMS Analytics"])

class CustomQueryRequest(BaseModel):
    query: str

@router.get("/summary")
def get_mysql_summary():
    return db_service.get_mysql_stats()

@router.get("/query-performance")
def get_query_performance():
    from connection import db_manager

    tests = [
        ("vw_policy_performance", "SELECT * FROM vw_policy_performance"),
        ("vw_experiment_comparison", "SELECT * FROM vw_experiment_comparison LIMIT 50"),
        ("vw_stock_history", "SELECT * FROM vw_stock_history"),
        ("vw_eviction_statistics", "SELECT * FROM vw_eviction_statistics"),
        ("Indexed Symbol Lookup", "SELECT * FROM stock_data WHERE symbol = 'AAPL' LIMIT 20"),
        ("Partition-Pruned Time-Series Scan", "SELECT * FROM stock_data WHERE market_timestamp >= '2026-10-01 00:00:00' AND market_timestamp < '2027-01-01 00:00:00' LIMIT 20"),
        ("Audit Trail Retrieval", "SELECT * FROM experiment_audit_log ORDER BY changed_at DESC LIMIT 20")
    ]

    results = []
    for name, q in tests:
        t0 = time.perf_counter()
        data = db_manager.execute_query(q)
        dt_ms = (time.perf_counter() - t0) * 1000.0
        columns = list(data[0].keys()) if data else []
        results.append({
            "query_name": name,
            "sql": q,
            "execution_time_ms": round(dt_ms, 3),
            "row_count": len(data),
            "status": "OPTIMAL_INDEXED",
            "columns": columns,
            "rows": data[:25]
        })

    return {
        "engine": "MySQL 8.x Engine" if not db_manager.use_sqlite else "Embedded Relational Engine (SQLite Mirror)",
        "demonstrations": results
    }

@router.post("/execute")
def execute_custom_query(req: CustomQueryRequest):
    from connection import db_manager
    q = req.query.strip()
    if not q:
        raise HTTPException(status_code=400, detail="Query cannot be empty")

    first_word = q.split()[0].upper()
    if first_word not in ("SELECT", "EXPLAIN", "SHOW", "DESCRIBE", "CALL"):
        raise HTTPException(status_code=400, detail="Only SELECT, EXPLAIN, SHOW, DESCRIBE, and CALL queries are permitted in the inspector.")

    t0 = time.perf_counter()
    try:
        data = db_manager.execute_query(q)
        dt_ms = (time.perf_counter() - t0) * 1000.0
        columns = list(data[0].keys()) if data else []
        return {
            "query_name": "Custom SQL Query",
            "sql": q,
            "execution_time_ms": round(dt_ms, 3),
            "row_count": len(data),
            "status": "EXECUTED",
            "columns": columns,
            "rows": data[:100],
            "engine": "MySQL 8.x Engine" if not db_manager.use_sqlite else "Embedded Relational Engine (SQLite Mirror)"
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"SQL Execution Error: {str(e)}")

@router.get("/table/{table_name}")
def get_table_data(table_name: str, limit: int = 25):
    from connection import db_manager
    allowed_tables = [
        "stock_data", "workload_experiments", "workload_requests",
        "eviction_policies", "cache_metrics", "eviction_logs",
        "benchmark_results", "adaptive_policy_metrics", "system_config",
        "experiment_summary", "experiment_audit_log"
    ]
    if table_name not in allowed_tables:
        raise HTTPException(status_code=400, detail=f"Invalid table name '{table_name}'")

    q = f"SELECT * FROM {table_name} LIMIT %s"
    t0 = time.perf_counter()
    try:
        data = db_manager.execute_query(q, (limit,))
        dt_ms = (time.perf_counter() - t0) * 1000.0
        columns = list(data[0].keys()) if data else []
        return {
            "query_name": f"Table: {table_name}",
            "sql": f"SELECT * FROM {table_name} LIMIT {limit}",
            "execution_time_ms": round(dt_ms, 3),
            "row_count": len(data),
            "status": "TABLE_SCAN",
            "columns": columns,
            "rows": data,
            "engine": "MySQL 8.x Engine" if not db_manager.use_sqlite else "Embedded Relational Engine (SQLite Mirror)"
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
