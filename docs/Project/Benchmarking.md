---
tags: [project/adaptive-cache-redis, benchmarking, evaluation, methodology, project/adaptive-cache-redis]
status: implemented
---

# Benchmarking and Evaluation

## What is being tested

> [!important] The hypothesis
> When the hot key set shifts suddenly — ticker popularity moving during an
> earnings event — does an **adaptive** policy outperform static ones (LRU, LFU,
> FIFO) on the same workload?
>
> This is the project's research question. Everything below exists to make the
> answer measurable rather than anecdotal.

---

## The workload

`Benchmark/generate_stock_workload.py` emits a yfinance-style CSV with a
**deliberate three-phase structure**:

| Rows | Phase | Weighting |
| --- | --- | --- |
| 0–300 | Tech-heavy | 10× toward `AAPL, NVDA, MSFT, TSLA` |
| 300–700 | Earnings shock | 15× toward `MRK, CVX, XOM, JPM, BAC` |
| 700–1000 | Broad scan | Uniform |

> [!tip] Why the phase shift matters
> The shift at row 300 is the entire experiment. A static policy cannot react
> to it: LRU will keep serving the old hot set until recency naturally decays,
> and FIFO is structurally incapable of reacting. An adaptive policy is
> supposed to notice. **The third phase matters just as much** — it tests
> whether the controller can give up a hot set that no longer deserves
> priority, rather than locking onto the new one and staying there.

Columns: `Datetime, Ticker, Close`.

### The operation mix

`Benchmark/benchmark_client.py` → `do_op()`
- With probability `--hit-target-percent` (default 80): `GET stock:<ticker>`,
  falling back to `SET` on a miss.
- Otherwise: `GET bench:missing:<worker>:<index>:<seed>` — a guaranteed miss,
  to measure the cost of misses without polluting the keyspace.

### Fresh connection per command

> [!important] A caveat to state before being asked
> Each command opens a **new TCP connection**. So the reported latency includes
> connection setup, and the numbers describe a connection-per-command client,
> not pipelined throughput. The server is thread-per-connection, so connection
> setup includes spawning an OS thread — visible in the concurrency-5 numbers.

---

## Metrics

| Metric | Source | What it means |
| --- | --- | --- |
| `total_evictions` | server `INFO` | Keys discarded to stay within capacity |
| `write_backs` | server `INFO` | Evictions that also cost a write — the I/O metric |
| `hit_rate_percent` | server `INFO` | Hits / (hits + misses) |
| `keys_stored` / `max_keys` | server `INFO` | Resident set vs. capacity |
| `*_selections` | server `INFO` | Which policy nominated victims |
| `adaptive_detail` | server `INFO` | Expert weights and shadow losses |
| `p95` / `p99` latency | client | Tail latency |
| `throughput_ops_sec` | client wall clock | Authoritative throughput |
| `approx_ops_per_second` | CSV timestamp span | Approximation only |

> [!warning] Before the fix, every server metric was `N/A`
> The benchmark client was already issuing `CONFIG SET POLICY`,
> `CONFIG SET MAXKEYS`, `CONFIG RESETSTAT` and `INFO`, but the server
> implemented none of them. The client degraded gracefully and printed
> "Notice: ...", so runs "succeeded" while measuring nothing. Wiring eviction
> and the counters is what turned this into a real evaluation.

---

## Running it

```bash
cd Redis-Server && make && ./my_redis_server 6379 dump.my_rdb 300
cd ../Benchmark
pip install -r requirements.txt

python generate_stock_workload.py

python benchmark_client.py --stock-csv stock_market_data.csv \
    --policy LRU --label LRU --max-keys 25 --operations 300 --concurrency 1 5
# repeat for LFU, FIFO, RANDOM, ADAPTIVE

python analyze_results.py --input results --output-dir analysis_output
```

The client issues, before measuring: `CONFIG SET POLICY`, `CONFIG SET MAXKEYS`,
`FLUSHALL`, then `CONFIG RESETSTAT` so counters start at zero.

Outputs: `benchmark_*.csv` (per-op), `summary_*.json` (per-run), and five
charts including `eviction_comparison.png`.

---

## Reading the results honestly

> [!warning] Four caveats to volunteer
> **1. Warm cache across concurrency levels.** Within one invocation the
> concurrency-5 pass is *not* flushed before running, so it starts from the
> state the concurrency-1 pass left. Use `--no-flush` for a cold start.
>
> **2. Latency partly measures policy cost.** LFU and FIFO select their victim
> with an O(n) scan; LRU and Random are O(1). Hit rate and evictions are the
> *quality* metrics — latency is not.
>
> **3. `approx_ops_per_second` is derived** from the span between the first and
> last CSV timestamps. The real wall-clock figure is `throughput_ops_sec` in the
> JSON.
>
> **4. Effect sizes are small at these operation counts.** At 300 operations
> with a 25-key cache, differences between policies are often within noise.
> Re-run with more operations before concluding anything.

> [!tip] A caveat about the benchmark itself
> `--hit-target-percent 80` targets an 80% hit rate; it does not achieve one.
> Actual hit rate is whatever the workload and the eviction decisions produce —
> which is precisely what the policies influence. Presenting the target as the
> result would be misleading.

### What to look for

| Question | Where to look |
| --- | --- |
| Did the adaptive policy adapt? | `adaptive_detail` weights moving away from 0.5/0.5 |
| Which expert won? | `preferred_expert` at the end of the run |
| Did the phase shift cost anything? | Hit rate before vs. after row 300 |
| Is any policy worse than random? | `RANDOM` is the null model — if LFU ties it, LFU is not using frequency |

---

## Related

- [[DBMS/Adaptive Replacement]] — the policy under test
- [[DBMS/Buffer Replacement Policies]] — the policies compared
- [[DBMS/Query Processing]] — what a real cost model would contribute
- [[Project/Gaps and Future Work]] — making the experiment stronger