# Control Panel & Dashboard — `ui_app.py`

A Streamlit app for driving and demonstrating the cache server: live statistics,
policy and capacity control, a key explorer, an eviction demo, benchmark charts,
and the Yahoo Finance stock-quote demo.

## Why not just use a Redis GUI?

Off-the-shelf Redis desktop tools (RedisInsight, Another Redis Desktop Manager,
and similar) **cannot connect** to this server. Every one of them opens a
session with `HELLO` to negotiate RESP3, and this server answers:

```
> HELLO 3
-ERR unknown command 'HELLO'
```

They also depend on `COMMAND DOCS`, `CLIENT LIST`, `CONFIG GET *`, `CLUSTER INFO`
and `SCAN`, none of which are implemented. There is no `SCAN` at all, so key
browsing has to scan — which is exactly what the **Keys** tab does.

## Running it

```bash
# 1. Build and start the server
cd Redis-Server
make
./my_redis_server 6379 dump.my_rdb 300

# 2. In another terminal, install the UI dependencies and launch
cd ..
pip install -r requirements-ui.txt
streamlit run ui_app.py
```

Streamlit serves on <http://localhost:8501>. `streamlit>=1.46` is required
because the dashboard uses `st.fragment(run_every=...)`.

Set the host and port in the sidebar if the server is not on `127.0.0.1:6379`.
If nothing is listening, the app says so and prints the command to start it
rather than failing with a stack trace.

## The tabs

### Dashboard
Auto-refreshing (every two seconds) view of the server's own counters:

- keys stored against capacity, with a fill bar
- hit rate, hits, misses, evictions
- write-backs — the evictions that actually cost a disk write, i.e. the I/O cost
- resident keys and cumulative evictions over time
- a bar chart of which policy nominated the victims
- for `ADAPTIVE`, the live expert weights and shadow losses

Toggle auto-refresh off in the sidebar for a static view, or clear the collected
history with the sidebar button.

### Control
- **Policy** — switch between `LRU`, `LFU`, `FIFO`, `RANDOM` and `ADAPTIVE`
  without restarting. The resident key set is handed to the new policy, so
  eviction keeps working immediately.
- **Capacity** — set the key limit. Lowering it evicts immediately until the
  cache fits; `0` removes the limit entirely.
- **Reset statistics** — zeroes the counters before a measured run.
- **FLUSHALL** — behind an explicit confirmation checkbox.

### Keys
Search with a glob pattern (`*`, `?`), then inspect any key: its type, TTL and
byte footprint. Strings can be edited and saved inline; lists and hashes render
as tables. Each described key costs three round trips (`TYPE`, `TTL`,
`MEMORY USAGE`), so the result set is capped by the *Max keys* slider.

### Eviction lab
The quickest way to demonstrate the whole point of the project. Set a small
capacity, press **Write keys (overflow)**, and watch the server discard keys to
stay within the limit. A tracked key flips between *still resident* and *has
been evicted*, with the live counters updating beside it.

### Benchmarks
Loads `Benchmark/analysis_output/analysis_summary.csv` if it exists and charts
hit rate, evictions and P95 latency per policy. If only the raw per-operation
CSVs are present it aggregates them itself, so the tab is useful even before
running `analyze_results.py`.

### Stock data
The original demo, kept. Fetches daily closes from Yahoo Finance and writes one
key per trading day as `yf:<TICKER>:close:<YYYY-MM-DD>`, plus a pointer key
`yf:<TICKER>:latest`. The survival table shows whether each latest-price record
is still resident — set a small capacity first and some will not be.

Lower the capacity in the **Control** tab before fetching if you want to see
records evicted.

## Key scheme

| Key | Contents |
| --- | --- |
| `yf:<TICKER>:close:<YYYY-MM-DD>` | one JSON object per trading day |
| `yf:<TICKER>:latest` | pointer to the most recent daily record |

## Notes and limitations
- Yahoo Finance access needs an internet connection and depends on
  yfinance/Yahoo availability. Every other tab works offline.
- The chart plots the data fetched from Yahoo; the survival table independently
  asks the server whether each record is still cached.
- `MAXKEYS` counts keys, not bytes.
- Live statistics come from `INFO`, so they are the server's own accounting
  rather than anything the UI infers.
- This is an educational demo, not investment advice.