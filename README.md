# Adaptive Cache Eviction for Real-Time Stock Quote Serving

A Redis-compatible in-memory caching server, built from scratch in C++, used to
run a comparative study of classical and adaptive cache eviction policies
against real historical stock market data.

> **Team 075 — Code Crackers** · Project-Based Learning, 5th Semester

---

## The question

Most cache eviction policies (LRU, LFU, FIFO) use a fixed rule for what to
discard when memory fills up. This project asks: when the "hot" data being
cached shifts suddenly — like ticker popularity during an earnings event — does
an **adaptive** eviction policy actually outperform the static ones?

## What is built

1. **A Redis-compatible server in C++** — RESP protocol over raw TCP sockets,
   strings/lists/hashes, TTLs, snapshots, multi-client concurrency, and a
   pluggable eviction engine.
2. **Five replacement policies** behind one interface — LRU, LFU, FIFO,
   Random, and an adaptive two-expert controller.
3. **A workload generator and benchmark harness** that replays historical stock
   quotes through the server and measures hit rate, evictions, latency and
   throughput.
4. **A Streamlit control panel** — live statistics, policy and capacity control,
   a key explorer, an eviction demo, benchmark charts, and a `yfinance` mode
   that writes real per-day quotes into the cache so you can watch real records
   get evicted.
5. **An Obsidian concept vault** (`docs/`) — every OS and DBMS concept mapped
   to the code that implements it, with an explicit statement of what is *not*
   implemented.

## Layout

```
Redis-Server/       the C++ server, eviction policies, and both test suites
Benchmark/          workload generator, benchmark client, analysis and charts
redis_client.py     reusable RESP client for the custom server
ui_app.py           Streamlit control panel and live dashboard
eviction_policies/  optional CMake build for the policy library
```

## Quick start

```bash
cd Redis-Server
make
make test                      # 1069 policy checks + 180 integration checks
./my_redis_server 6379 dump.my_rdb 300
```

In another terminal:

```bash
cd Benchmark
pip install -r requirements.txt
python generate_stock_workload.py
python benchmark_client.py --stock-csv stock_market_data.csv \
    --policy ADAPTIVE --label ADAPTIVE --max-keys 25 --operations 300 --concurrency 1 5
python analyze_results.py --input results
```

Then `streamlit run ui_app.py` for the GUI.

## Documentation

- [`Redis-Server/README.md`](Redis-Server/README.md) — architecture, command set, design decisions
- [`eviction_policies/README.md`](eviction_policies/README.md) — the policies, the contract, and the adaptive controller
- [`Benchmark/README.md`](Benchmark/README.md) — methodology and how to read the results
- [`CONTROL_PANEL_GUIDE.md`](CONTROL_PANEL_GUIDE.md) — the Streamlit control panel
- [`docs/Home.md`](docs/Home.md) — **concept vault**: every OS/DBMS concept ↔ `file:line`

### Concept vault

`docs/` is an Obsidian vault (open that folder, not the repo root). 31 notes
covering 10 OS concepts, 10 DBMS concepts, and this codebase's internals — each
with the code that implements it, and explicit callouts where something is
**not** implemented. Start at `docs/Home.md`.

```bash
python3 tools/validate_vault.py   # checks links and all 261 source references
```

### Note on GUI tools

Standard Redis desktop tools (RedisInsight, Another Redis Desktop Manager) cannot
connect to this server: they open a session with `HELLO` to negotiate RESP3 and
then rely on `COMMAND DOCS`, `CLIENT LIST`, `CONFIG GET *`, `CLUSTER INFO` and
`SCAN`, none of which are implemented. Use the bundled Streamlit panel instead.

## A note on naming

The adaptive policy is **not** CacheUS. It is CacheUS-inspired: two experts
(LRU and LFU) scored in the shadow, with multiplicative weights updated against
the lower-loss expert. It has no sliding window, no learned cost predictor, no
gradient training and no ghost cache. It is called `ADAPTIVE` throughout, and
`CACHEUS` is accepted only as a legacy alias so older scripts keep running.