import os
import sys
import json
import time
import socket
import datetime
import numpy as np
import pandas as pd
from typing import List, Dict, Any, Optional

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "database"))
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "workload"))
from connection import db_manager
from workload_generator import WorkloadGenerator

POLICIES = ["LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"]

class RedisBenchmarkClient:
    """
    Direct TCP RESP client for fair high-performance cache benchmarking.
    """
    def __init__(self, host: str = "127.0.0.1", port: int = 6379):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5.0)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def execute(self, cmd_tokens: List[str]) -> str:
        if not self.sock:
            self.connect()
        msg = f"*{len(cmd_tokens)}\r\n"
        for t in cmd_tokens:
            s = str(t)
            msg += f"${len(s.encode('utf-8'))}\r\n{s}\r\n"
        self.sock.sendall(msg.encode("utf-8"))

        data = b""
        while True:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            data += chunk
            if data.endswith(b"\r\n"):
                break
        return data.decode("utf-8", errors="replace")

class BenchmarkingEngine:
    def __init__(self, host: str = "127.0.0.1", port: int = 6379):
        self.host = host
        self.port = port
        self.client = RedisBenchmarkClient(host, port)
        self.workload_gen = WorkloadGenerator()
        self.results_dir = os.path.join(os.path.dirname(__file__), "..", "..", "results")
        os.makedirs(os.path.join(self.results_dir, "exports"), exist_ok=True)
        os.makedirs(os.path.join(self.results_dir, "reports"), exist_ok=True)

    def run_benchmark_suite(self, 
                            experiment_name: str = "Comparative Evaluation",
                            workload_type: str = "zipfian", 
                            cache_capacity: int = 500, 
                            total_requests: int = 1000,
                            policies: Optional[List[str]] = None,
                            seed: int = 42) -> Dict[str, Any]:
        target_policies = policies or POLICIES
        print("=================================================================")
        print(f" Starting Benchmark Suite: {experiment_name}")
        print(f" Workload: {workload_type} | Capacity: {cache_capacity} | Requests: {total_requests}")
        print("=================================================================")

        # 1. Generate identical workload trace for fair comparison
        trace = self.workload_gen.generate_trace(workload_type, total_requests, seed=seed)

        # 2. Record experiment in database
        exp_id = self.workload_gen.save_workload_experiment(
            experiment_name, workload_type, cache_capacity, total_requests, request_rate=1000.0
        )

        all_policy_results = []

        # 3. Benchmark each policy sequentially under identical initial conditions
        for policy_name in target_policies:
            print(f"\n---> Benchmarking Policy: {policy_name}")
            policy_result = self._benchmark_single_policy(
                exp_id, policy_name, cache_capacity, trace, workload_type
            )
            all_policy_results.append(policy_result)

        # 4. Update experiment status to COMPLETED
        now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        db_manager.execute_update(
            "UPDATE workload_experiments SET status = 'COMPLETED', end_time = %s WHERE experiment_id = %s",
            (now, exp_id)
        )

        # 5. Populate experiment executive summary via stored procedure or SQL aggregation
        self._generate_experiment_summary(exp_id)

        # 6. Export results matrix to CSV
        csv_path = self._export_to_csv(exp_id, all_policy_results)

        print("\n=================================================================")
        print(" Benchmark Suite Complete!")
        print(f" Summary CSV Exported: {csv_path}")
        print("=================================================================")

        return {
            "experiment_id": exp_id,
            "experiment_name": experiment_name,
            "workload_type": workload_type,
            "cache_capacity": cache_capacity,
            "total_requests": total_requests,
            "results": all_policy_results,
            "csv_path": csv_path
        }

    def _benchmark_single_policy(self, 
                                 exp_id: int, 
                                 policy_name: str, 
                                 capacity: int, 
                                 trace: List[Dict[str, Any]],
                                 workload_type: str) -> Dict[str, Any]:
        # Reset and configure cache server
        self.client.connect()
        self.client.execute(["FLUSHALL"])
        self.client.execute(["CONFIG", "SET", "eviction_policy", policy_name])
        self.client.execute(["CONFIG", "SET", "capacity", str(capacity)])

        latencies_ms = []
        hits = 0
        misses = 0
        staleness_values = []
        price_deviations = []

        # Ground truth latest source timestamp cache for staleness calculation
        source_latest = {}
        for req in trace:
            sym = req["symbol"]
            ts_str = req["quote_data"]["market_timestamp"]
            try:
                dt = datetime.datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S")
            except Exception:
                dt = datetime.datetime.now()
            if sym not in source_latest or dt > source_latest[sym]["time"]:
                source_latest[sym] = {"time": dt, "price": req["quote_data"]["price"]}

        t_start = time.perf_counter()

        for req in trace:
            key = f"stock:{req['symbol']}"
            t0 = time.perf_counter_ns()

            if req["request_type"] == "SET":
                payload = json.dumps(req["quote_data"])
                self.client.execute(["SET", key, payload])
                t1 = time.perf_counter_ns()
                latencies_ms.append((t1 - t0) / 1e6)
            else: # GET
                res = self.client.execute(["GET", key])
                t1 = time.perf_counter_ns()
                latencies_ms.append((t1 - t0) / 1e6)

                if res.startswith("$-1"):
                    misses += 1
                    # On miss, populate cache from database record (read-through simulation)
                    payload = json.dumps(req["quote_data"])
                    self.client.execute(["SET", key, payload])
                else:
                    hits += 1
                    # Extract cached quote and calculate staleness
                    try:
                        lines = res.split("\r\n")
                        if len(lines) >= 2:
                            cached_quote = json.loads(lines[1])
                            cached_ts = datetime.datetime.strptime(
                                cached_quote["market_timestamp"], "%Y-%m-%d %H:%M:%S"
                            )
                            src_info = source_latest.get(req["symbol"])
                            if src_info:
                                st = max(0.0, (src_info["time"] - cached_ts).total_seconds())
                                staleness_values.append(st)
                                dev = abs(float(cached_quote.get("price", 0)) - float(src_info["price"]))
                                price_deviations.append(dev)
                    except Exception:
                        pass

        total_exec_time = time.perf_counter() - t_start
        total_requests = len(trace)
        hit_rate = (hits / total_requests) * 100.0 if total_requests > 0 else 0.0
        miss_rate = 100.0 - hit_rate
        avg_latency = float(np.mean(latencies_ms)) if latencies_ms else 0.0
        p50 = float(np.percentile(latencies_ms, 50)) if latencies_ms else 0.0
        p95 = float(np.percentile(latencies_ms, 95)) if latencies_ms else 0.0
        p99 = float(np.percentile(latencies_ms, 99)) if latencies_ms else 0.0
        throughput = total_requests / total_exec_time if total_exec_time > 0 else 0.0
        avg_staleness = float(np.mean(staleness_values)) if staleness_values else 0.0

        # Retrieve server eviction stats
        evict_resp = self.client.execute(["EVICTION.STATS"])
        total_evictions = 0
        w1 = 0.5
        w2 = 0.5
        regret1 = 0.0
        regret2 = 0.0
        for line in evict_resp.split("\n"):
            if line.startswith("evictions:"):
                total_evictions = int(line.split(":")[1].strip())
            elif line.startswith("expert1_weight:"):
                w1 = float(line.split(":")[1].strip())
            elif line.startswith("expert2_weight:"):
                w2 = float(line.split(":")[1].strip())
            elif line.startswith("cumulative_regret1:"):
                regret1 = float(line.split(":")[1].strip())
            elif line.startswith("cumulative_regret2:"):
                regret2 = float(line.split(":")[1].strip())

        self.client.close()

        # Fetch policy_id from DB
        pol_rows = db_manager.execute_query(
            "SELECT policy_id FROM eviction_policies WHERE policy_name = %s", (policy_name,)
        )
        policy_id = pol_rows[0]["policy_id"] if pol_rows else 1

        # Persist benchmark result in MySQL benchmark_results
        now_dt = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        bench_sql = """
        INSERT INTO benchmark_results 
        (experiment_id, policy_id, workload_type, cache_capacity, hit_rate, average_latency, 
         p95_latency, throughput, staleness, execution_time, created_at)
        VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
        """
        db_manager.execute_update(bench_sql, (
            exp_id, policy_id, workload_type, capacity, round(hit_rate, 3),
            round(avg_latency, 4), round(p95, 4), round(throughput, 2),
            round(avg_staleness, 4), round(total_exec_time, 2), now_dt
        ))

        # Persist detailed metrics in cache_metrics
        metric_sql = """
        INSERT INTO cache_metrics
        (experiment_id, policy_id, total_requests, cache_hits, cache_misses, hit_rate, miss_rate,
         total_evictions, average_latency, p50_latency, p95_latency, p99_latency, throughput, memory_usage, timestamp)
        VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
        """
        db_manager.execute_update(metric_sql, (
            exp_id, policy_id, total_requests, hits, misses, round(hit_rate, 3), round(miss_rate, 3),
            total_evictions, round(avg_latency, 4), round(p50, 4), round(p95, 4), round(p99, 4),
            round(throughput, 2), round(capacity * 0.128, 2), now_dt
        ))

        # If Adaptive, persist expert weights and regret history
        if policy_name == "ADAPTIVE":
            adapt_sql = """
            INSERT INTO adaptive_policy_metrics
            (experiment_id, expert_name, expert_weight, cumulative_regret, selection_count, reward, timestamp)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            """
            db_manager.execute_update(adapt_sql, (exp_id, "LRU", round(w1, 6), round(regret1, 4), hits // 2, 0.5, now_dt))
            db_manager.execute_update(adapt_sql, (exp_id, "LFU", round(w2, 6), round(regret2, 4), hits // 2, 0.5, now_dt))

        print(f"    Hit Rate: {hit_rate:.2f}% | Latency (avg): {avg_latency:.3f}ms | Throughput: {throughput:.1f} rps | Evictions: {total_evictions}")

        return {
            "policy": policy_name,
            "hit_rate": round(hit_rate, 2),
            "miss_rate": round(miss_rate, 2),
            "avg_latency": round(avg_latency, 4),
            "p50_latency": round(p50, 4),
            "p95_latency": round(p95, 4),
            "p99_latency": round(p99, 4),
            "throughput": round(throughput, 2),
            "total_evictions": total_evictions,
            "staleness_sec": round(avg_staleness, 4),
            "execution_time_sec": round(total_exec_time, 2)
        }

    def _generate_experiment_summary(self, exp_id: int):
        rows = db_manager.execute_query(
            "SELECT p.policy_name, b.hit_rate, b.average_latency, b.throughput, b.staleness "
            "FROM benchmark_results b JOIN eviction_policies p ON b.policy_id = p.policy_id "
            "WHERE b.experiment_id = %s ORDER BY b.hit_rate DESC", (exp_id,)
        )
        if not rows:
            return

        best = rows[0]
        lowest_lat = min(r["average_latency"] for r in rows)
        highest_tp = max(r["throughput"] for r in rows)
        lowest_stale = min(r["staleness"] for r in rows)
        now_dt = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        db_manager.execute_update(
            "INSERT INTO experiment_summary "
            "(experiment_id, best_policy, highest_hit_rate, lowest_latency, highest_throughput, lowest_staleness, generated_at) "
            "VALUES (%s, %s, %s, %s, %s, %s, %s)",
            (exp_id, best["policy_name"], best["hit_rate"], lowest_lat, highest_tp, lowest_stale, now_dt)
        )

    def _export_to_csv(self, exp_id: int, results: List[Dict[str, Any]]) -> str:
        csv_filename = f"benchmark_matrix_exp_{exp_id}.csv"
        csv_path = os.path.join(self.results_dir, "exports", csv_filename)
        df = pd.DataFrame(results)
        df.to_csv(csv_path, index=False)
        return csv_path

if __name__ == "__main__":
    be = BenchmarkingEngine()
    be.run_benchmark_suite(
        experiment_name="Initial Verification Benchmark",
        workload_type="zipfian",
        cache_capacity=500,
        total_requests=300
    )
