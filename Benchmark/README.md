# Benchmarking Client & Performance Analysis — Adaptive Cache Redis

This module benchmarks and analyzes the Redis-compatible in-memory caching server under real or synthetic stock quote workloads.

## Features
- **Pluggable Policy Benchmarking:** Benchmarks **FIFO**, **LRU**, **LFU**, **RANDOM**, and **CACHEUS** (Adaptive Recency + Frequency with Ghost Caching).
- **Dynamic Policy Switching:** Automatically issues `CONFIG SET POLICY <name>` and `CONFIG SET MAXKEYS <capacity>` to the server over RESP protocol.
- **Server-Side Eviction Metrics:** Queries `INFO stats` to record real server-side cache evictions, hits, misses, and hit-rate percentages.
- **Realistic Stock Market Workload:** Replays historical stock market CSV datasets (e.g. `stock_market_data.csv`).
- **Comprehensive Analysis & Visualization:** Aggregates metrics with Pandas and plots comparative latency curves, P95 latency, throughput, observed hit rates, and eviction counts with Matplotlib.

---

## Quick Start (Windows)

### 1. Start the Redis Server
In a terminal, navigate to `Redis-Server` and start the server:
```powershell
cd Redis-Server
.\my_redis_server.exe 6379 LRU 25
```
*(Optionally specify port, initial eviction policy, and max key capacity).*

### 2. Install Python Dependencies
In a second terminal, navigate to `Python pbl1`:
```powershell
cd "Python pbl1"
pip install -r requirements.txt
```

### 3. Run the Automated Multi-Policy Comparison
Run the automated batch script to test all 5 policies (FIFO, LRU, LFU, RANDOM, CACHEUS):
```cmd
run_experiments.bat
```

Or run individual policy benchmarks manually:
```bash
python benchmark_client.py --stock-csv stock_market_data.csv --policy FIFO --label FIFO --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results
python benchmark_client.py --stock-csv stock_market_data.csv --policy LRU --label LRU --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results
python benchmark_client.py --stock-csv stock_market_data.csv --policy LFU --label LFU --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results
python benchmark_client.py --stock-csv stock_market_data.csv --policy RANDOM --label RANDOM --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results
python benchmark_client.py --stock-csv stock_market_data.csv --policy CACHEUS --label CACHEUS --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results
```

### 4. Generate Analysis & Charts
```bash
python analyze_results.py --input results --output-dir analysis_output
```

---

## Outputs & Artifacts
- `results/benchmark_*.csv`: Per-operation latencies, response payloads, keys, and hit/miss logs.
- `results/summary_*.json`: Summary stats containing wall-clock throughput, latency percentiles, and real server-side eviction counts.
- `analysis_output/analysis_summary.csv`: Aggregated comparative table across all policies and concurrency levels.
- `analysis_output/analysis_report.md`: Markdown summary and architectural interpretation.
- `analysis_output/hit_rate_comparison.png`: Cache hit rate comparison across policies.
- `analysis_output/latency_comparison.png`: Average latency comparison.
- `analysis_output/p95_latency_comparison.png`: 95th-percentile tail latency comparison.
- `analysis_output/throughput_comparison.png`: Throughput comparison (operations/second).
- `analysis_output/eviction_comparison.png`: Total eviction count comparison.
