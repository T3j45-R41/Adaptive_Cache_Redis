# Benchmarking Client & Performance Analysis — Adaptive Cache Redis

This module replays stock quote workloads through the cache server and compares
the five replacement policies against each other.

## What it measures
- **Server-side evictions** — read from `INFO`, so the numbers come from the
  server's own accounting rather than the client's guess.
- **Hit rate** — both client-observed and server-side.
- **Latency and throughput** — measured end-to-end from the client. Each
  command uses a fresh TCP connection, so these numbers include connection
  setup and reflect a connection-per-command client, not pipelined throughput.
- **Source data staleness** — how old each cached quote was at the moment it
  was replayed.

## Policies
`LRU`, `LFU`, `FIFO`, `RANDOM`, and `ADAPTIVE`.

`ADAPTIVE` is the two-expert exponential-weights controller (LRU and LFU experts
scored in the shadow, weights updated multiplicatively against the lower-loss
expert with decayed loss). It is **not** a reproduction of CacheUS: there is no
sliding access window, no learned cost predictor, no gradient training, and no
ghost cache. `CACHEUS` is still accepted on the command line as a legacy alias
for `ADAPTIVE`, so older scripts keep working.

## Quick Start

### 1. Build and start the server
```bash
cd ../Redis-Server
make
./my_redis_server 6379 dump.my_rdb 300
```
Arguments are `port`, `snapshot-file`, and `snapshot-interval-seconds`
(`0` disables periodic snapshots). The active policy and capacity are changed
at runtime with `CONFIG SET` or the `EVICTION` / `MAXKEYS` shorthands — they are
not command-line flags.

### 2. Install dependencies
```bash
cd ../Benchmark
pip install -r requirements.txt
```

### 3. Generate a workload (optional)
The benchmark falls back to a synthetic keyspace if no CSV is supplied. To use
real data:
```bash
python generate_stock_workload.py
```
This writes `stock_market_data.csv` with three deliberate phases — a
tech-heavy stretch, an earnings-shock phase weighted toward health/energy
tickers, then a uniform broad scan. The phase shift at the midpoint is the
whole point: it is what an adaptive policy is supposed to react to and a static
one cannot.

### 4. Run the comparison
```bash
python benchmark_client.py --policy LRU     --label LRU     --max-keys 25 --operations 300 --concurrency 1 5
python benchmark_client.py --policy LFU     --label LFU     --max-keys 25 --operations 300 --concurrency 1 5
python benchmark_client.py --policy FIFO    --label FIFO    --max-keys 25 --operations 300 --concurrency 1 5
python benchmark_client.py --policy RANDOM  --label RANDOM  --max-keys 25 --operations 300 --concurrency 1 5
python benchmark_client.py --policy ADAPTIVE --label ADAPTIVE --max-keys 25 --operations 300 --concurrency 1 5
```

Or use the stock CSV instead of the synthetic keyspace:
```bash
python benchmark_client.py --stock-csv stock_market_data.csv --policy ADAPTIVE --label ADAPTIVE --max-keys 25 --operations 300 --concurrency 1 5
```

The client issues, before measuring: `CONFIG SET POLICY <name>`,
`CONFIG SET MAXKEYS <n>`, `FLUSHALL`, then `CONFIG RESETSTAT` so the counters
start from zero. It reads results back with `INFO`.

### 5. Analyse
```bash
python analyze_results.py --input results --output-dir analysis_output
```

## Server commands this module relies on

| Command | Purpose |
| --- | --- |
| `CONFIG SET POLICY <name>` | Switch the active replacement policy |
| `CONFIG SET MAXKEYS <n>` | Set the capacity limit (`0` = unlimited) |
| `CONFIG RESETSTAT` | Zero the counters before a measured run |
| `CONFIG GET <param>` | Read back `POLICY`, `MAXKEYS`, `DBSIZE`, `BYTES` |
| `EVICTION [<name>]` | Shorthand: get or set the active policy |
| `MAXKEYS [<n>]` | Shorthand: get or set the capacity limit |
| `INFO` | Full counters snapshot as a bulk string of `key:value` lines |
| `DBSIZE` | Current resident key count |
| `PING`, `SET`, `GET`, `FLUSHALL` | Workload and health checks |

## Outputs
- `results/benchmark_*.csv` — per-operation latency, key, hit/miss, staleness.
- `results/summary_*.json` — per-run config, final `INFO` snapshot, and summaries.
- `analysis_output/analysis_summary.csv` — comparative table per policy and
  concurrency level, including `server_eviction_count`.
- `analysis_output/analysis_report.md` — generated interpretation.
- `analysis_output/*.png` — latency, P95 latency, throughput, hit rate, and
  eviction comparisons.

## Reading the results honestly
- The two concurrency levels run against **the same server without a flush in
  between**, so the concurrency-5 pass starts from the warm cache the
  concurrency-1 pass left. Use `--no-flush` on the second run if you want each
  level measured from cold.
- `approx_ops_per_second` in the summary CSV is derived from the span between
  the first and last recorded operation timestamps. The authoritative
  wall-clock throughput is `throughput_ops_sec` in `summary_*.json`.
- Differences between policies at this operation count are usually small.
  Re-run with more operations before drawing conclusions.