import sys
import os
from typing import List, Dict, Any, Optional

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python", "database"))
from connection import db_manager

class DatabaseService:
    @staticmethod
    def get_experiments(limit: int = 50) -> List[Dict[str, Any]]:
        query = "SELECT * FROM workload_experiments ORDER BY experiment_id DESC LIMIT %s"
        return db_manager.execute_query(query, (limit,))

    @staticmethod
    def get_experiment_by_id(exp_id: int) -> Optional[Dict[str, Any]]:
        rows = db_manager.execute_query(
            "SELECT * FROM workload_experiments WHERE experiment_id = %s", (exp_id,)
        )
        return rows[0] if rows else None

    @staticmethod
    def get_benchmark_results(exp_id: Optional[int] = None) -> List[Dict[str, Any]]:
        if exp_id:
            query = """
            SELECT b.*, p.policy_name, p.is_adaptive 
            FROM benchmark_results b 
            JOIN eviction_policies p ON b.policy_id = p.policy_id 
            WHERE b.experiment_id = %s 
            ORDER BY b.hit_rate DESC
            """
            return db_manager.execute_query(query, (exp_id,))
        else:
            query = """
            SELECT b.*, p.policy_name, p.is_adaptive, e.experiment_name 
            FROM benchmark_results b 
            JOIN eviction_policies p ON b.policy_id = p.policy_id 
            JOIN workload_experiments e ON b.experiment_id = e.experiment_id
            ORDER BY b.result_id DESC LIMIT 100
            """
            return db_manager.execute_query(query)

    @staticmethod
    def get_comparison_view() -> List[Dict[str, Any]]:
        return db_manager.execute_query("SELECT * FROM vw_experiment_comparison LIMIT 100")

    @staticmethod
    def get_policy_performance_view() -> List[Dict[str, Any]]:
        return db_manager.execute_query("SELECT * FROM vw_policy_performance")

    @staticmethod
    def get_stock_history_view() -> List[Dict[str, Any]]:
        return db_manager.execute_query("SELECT * FROM vw_stock_history")

    @staticmethod
    def get_eviction_statistics_view() -> List[Dict[str, Any]]:
        return db_manager.execute_query("SELECT * FROM vw_eviction_statistics")

    @staticmethod
    def get_adaptive_weights(exp_id: Optional[int] = None) -> List[Dict[str, Any]]:
        if exp_id:
            return db_manager.execute_query(
                "SELECT * FROM adaptive_policy_metrics WHERE experiment_id = %s ORDER BY timestamp ASC",
                (exp_id,)
            )
        return db_manager.execute_query("SELECT * FROM adaptive_policy_metrics ORDER BY timestamp ASC LIMIT 200")

    @staticmethod
    def get_latest_stocks() -> List[Dict[str, Any]]:
        query = """
        SELECT s1.* FROM stock_data s1
        INNER JOIN (
            SELECT symbol, MAX(market_timestamp) AS max_time
            FROM stock_data GROUP BY symbol
        ) s2 ON s1.symbol = s2.symbol AND s1.market_timestamp = s2.max_time
        ORDER BY s1.symbol ASC
        """
        return db_manager.execute_query(query)

    @staticmethod
    def get_mysql_stats() -> Dict[str, Any]:
        tables = [
            "stock_data", "workload_experiments", "workload_requests",
            "eviction_policies", "cache_metrics", "eviction_logs",
            "benchmark_results", "adaptive_policy_metrics", "system_config",
            "experiment_summary", "experiment_audit_log"
        ]
        counts = {}
        for t in tables:
            try:
                res = db_manager.execute_query(f"SELECT COUNT(*) as count FROM {t}")
                counts[t] = res[0]["count"] if res else 0
            except Exception:
                counts[t] = 0
        return {
            "rdbms_type": "MySQL 8.x" if not db_manager.use_sqlite else "Embedded Relational Engine (SQLite Mirror)",
            "database_name": "adaptive_cache_db",
            "tables": counts,
            "total_records": sum(counts.values())
        }

db_service = DatabaseService()
