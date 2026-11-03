import os
import pymysql
import sqlite3
import json
from datetime import datetime
from typing import Dict, Any, List, Optional

class DatabaseManager:
    """
    Dual-mode Database Manager for MySQL 8.x and embedded SQLite backup.
    Supports ACID transactions, prepared statements, and query execution.
    """
    def __init__(self):
        self.host = os.getenv("MYSQL_HOST", "127.0.0.1")
        self.port = int(os.getenv("MYSQL_PORT", 3306))
        self.user = os.getenv("MYSQL_USER", "root")
        self.password = os.getenv("MYSQL_PASSWORD", "")
        self.database = os.getenv("MYSQL_DATABASE", "adaptive_cache_db")
        self.use_sqlite = False
        self.sqlite_db_path = os.path.join(os.path.dirname(__file__), "..", "..", "results", "adaptive_cache.sqlite3")
        os.makedirs(os.path.dirname(self.sqlite_db_path), exist_ok=True)
        self._test_connection()

    def _test_connection(self):
        try:
            conn = pymysql.connect(
                host=self.host,
                port=self.port,
                user=self.user,
                password=self.password,
                connect_timeout=2
            )
            cursor = conn.cursor()
            cursor.execute(f"CREATE DATABASE IF NOT EXISTS {self.database}")
            cursor.close()
            conn.close()
            self.use_sqlite = False
            print(f"[DBMS] Connected successfully to MySQL at {self.host}:{self.port}/{self.database}")
        except Exception as e:
            self.use_sqlite = True
            print(f"[DBMS] MySQL not available ({e}). Using embedded SQLite database mirror at {self.sqlite_db_path}")
            self._init_sqlite()

    def get_connection(self):
        if not self.use_sqlite:
            try:
                return pymysql.connect(
                    host=self.host,
                    port=self.port,
                    user=self.user,
                    password=self.password,
                    database=self.database,
                    charset="utf8mb4",
                    cursorclass=pymysql.cursors.DictCursor,
                    autocommit=False
                )
            except Exception:
                self.use_sqlite = True
                return self.get_sqlite_connection()
        else:
            return self.get_sqlite_connection()

    def get_sqlite_connection(self):
        conn = sqlite3.connect(self.sqlite_db_path)
        conn.row_factory = sqlite3.Row
        return conn

    def _init_sqlite(self):
        conn = self.get_sqlite_connection()
        cursor = conn.cursor()
        cursor.executescript("""
        CREATE TABLE IF NOT EXISTS stock_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            price REAL NOT NULL,
            open_price REAL NOT NULL,
            high_price REAL NOT NULL,
            low_price REAL NOT NULL,
            volume INTEGER NOT NULL,
            market_timestamp TEXT NOT NULL,
            inserted_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS eviction_policies (
            policy_id INTEGER PRIMARY KEY AUTOINCREMENT,
            policy_name TEXT NOT NULL UNIQUE,
            policy_description TEXT,
            is_adaptive INTEGER NOT NULL DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS workload_experiments (
            experiment_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_name TEXT NOT NULL,
            workload_type TEXT NOT NULL,
            cache_capacity INTEGER NOT NULL,
            total_requests INTEGER NOT NULL,
            request_rate REAL NOT NULL,
            start_time TEXT NOT NULL,
            end_time TEXT,
            status TEXT NOT NULL DEFAULT 'PENDING'
        );

        CREATE TABLE IF NOT EXISTS workload_requests (
            request_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            sequence_num INTEGER NOT NULL,
            symbol TEXT NOT NULL,
            request_type TEXT NOT NULL DEFAULT 'GET',
            scheduled_delay_ms INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS cache_metrics (
            metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            policy_id INTEGER NOT NULL,
            total_requests INTEGER NOT NULL,
            cache_hits INTEGER NOT NULL,
            cache_misses INTEGER NOT NULL,
            hit_rate REAL NOT NULL,
            miss_rate REAL NOT NULL,
            total_evictions INTEGER NOT NULL,
            average_latency REAL NOT NULL,
            p50_latency REAL NOT NULL,
            p95_latency REAL NOT NULL,
            p99_latency REAL NOT NULL,
            throughput REAL NOT NULL,
            memory_usage REAL NOT NULL,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE,
            FOREIGN KEY (policy_id) REFERENCES eviction_policies(policy_id)
        );

        CREATE TABLE IF NOT EXISTS eviction_logs (
            eviction_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            policy_id INTEGER NOT NULL,
            evicted_key TEXT NOT NULL,
            eviction_reason TEXT NOT NULL DEFAULT 'capacity_exceeded',
            access_frequency INTEGER DEFAULT 1,
            last_access_time TEXT,
            eviction_timestamp TEXT NOT NULL,
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE,
            FOREIGN KEY (policy_id) REFERENCES eviction_policies(policy_id)
        );

        CREATE TABLE IF NOT EXISTS adaptive_policy_metrics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            expert_name TEXT NOT NULL,
            expert_weight REAL NOT NULL,
            cumulative_regret REAL NOT NULL,
            selection_count INTEGER NOT NULL DEFAULT 0,
            reward REAL NOT NULL DEFAULT 0.0,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS benchmark_results (
            result_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            policy_id INTEGER NOT NULL,
            workload_type TEXT NOT NULL,
            cache_capacity INTEGER NOT NULL,
            hit_rate REAL NOT NULL,
            average_latency REAL NOT NULL,
            p95_latency REAL NOT NULL,
            throughput REAL NOT NULL,
            staleness REAL NOT NULL,
            execution_time REAL NOT NULL,
            created_at TEXT NOT NULL,
            UNIQUE(experiment_id, policy_id),
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE,
            FOREIGN KEY (policy_id) REFERENCES eviction_policies(policy_id)
        );

        CREATE TABLE IF NOT EXISTS system_config (
            config_id INTEGER PRIMARY KEY AUTOINCREMENT,
            config_key TEXT NOT NULL UNIQUE,
            config_value TEXT NOT NULL,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS experiment_summary (
            summary_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL UNIQUE,
            best_policy TEXT NOT NULL,
            highest_hit_rate REAL NOT NULL,
            lowest_latency REAL NOT NULL,
            highest_throughput REAL NOT NULL,
            lowest_staleness REAL NOT NULL,
            generated_at TEXT NOT NULL,
            FOREIGN KEY (experiment_id) REFERENCES workload_experiments(experiment_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS experiment_audit_log (
            audit_id INTEGER PRIMARY KEY AUTOINCREMENT,
            experiment_id INTEGER NOT NULL,
            old_status TEXT,
            new_status TEXT,
            action_type TEXT NOT NULL,
            changed_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        -- Views
        CREATE VIEW IF NOT EXISTS vw_policy_performance AS
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

        CREATE VIEW IF NOT EXISTS vw_experiment_comparison AS
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

        CREATE VIEW IF NOT EXISTS vw_stock_history AS
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

        CREATE VIEW IF NOT EXISTS vw_eviction_statistics AS
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

        -- Seed Policies
        INSERT OR IGNORE INTO eviction_policies (policy_id, policy_name, policy_description, is_adaptive) VALUES
        (1, 'LRU', 'Least Recently Used: Discards items not accessed for the longest period.', 0),
        (2, 'LFU', 'Least Frequently Used: Discards items with lowest access count with recency tie-breaking.', 0),
        (3, 'FIFO', 'First-In First-Out: Evicts items in strict chronological order of initial insertion.', 0),
        (4, 'RANDOM', 'Random Replacement: Evicts a randomly selected key uniformly across all active entries.', 0),
        (5, 'ADAPTIVE', 'Adaptive CACHEUS-style: Dynamic regret-minimizing ensemble between recency and frequency experts.', 1);
        """)
        conn.commit()
        conn.close()

    def execute_query(self, query: str, params: tuple = ()) -> List[Dict[str, Any]]:
        conn = self.get_connection()
        cursor = conn.cursor()
        try:
            if self.use_sqlite:
                # Replace %s with ? for SQLite compatibility
                sqlite_query = query.replace("%s", "?")
                cursor.execute(sqlite_query, params)
                rows = cursor.fetchall()
                return [dict(row) for row in rows]
            else:
                cursor.execute(query, params)
                return cursor.fetchall()
        finally:
            cursor.close()
            conn.close()

    def execute_update(self, query: str, params: tuple = ()) -> int:
        conn = self.get_connection()
        cursor = conn.cursor()
        try:
            if self.use_sqlite:
                sqlite_query = query.replace("%s", "?")
                cursor.execute(sqlite_query, params)
                last_id = cursor.lastrowid
                conn.commit()
                return last_id
            else:
                cursor.execute(query, params)
                last_id = cursor.lastrowid
                conn.commit()
                return last_id
        except Exception as e:
            conn.rollback()
            raise e
        finally:
            cursor.close()
            conn.close()

# Global Singleton Database Manager
db_manager = DatabaseManager()
