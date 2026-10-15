# Yahoo Finance + Streamlit UI for Adaptive Cache Redis

This optional Python UI fetches daily stock history with `yfinance`, writes each daily close and a latest-price record into the project's C++ Redis-compatible server using RESP over TCP, and displays a price chart plus whether the latest record remains in the cache.

## 1. Start the C++ server first

Open PowerShell in `Redis-Server` and start the server in its own terminal:

```powershell
.\my_redis_server.exe 6379
```

If you have changed source code, rebuild first using the Makefile command supported by your toolchain, e.g. `mingw32-make clean` followed by `mingw32-make`.

## 2. Install the UI dependencies

Open a **second** PowerShell terminal in the top-level `Adaptive_Cache_Redis-main` folder:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements-ui.txt
```

If PowerShell blocks activation, you can skip activation and use `.\.venv\Scripts\python.exe -m pip install -r requirements-ui.txt` and `.\.venv\Scripts\python.exe -m streamlit run ui_app.py` instead.

## 3. Launch the UI

```powershell
python -m streamlit run ui_app.py
```

Streamlit prints a local URL, usually `http://localhost:8501`. Open it in your browser.

## 4. Use the demo

1. Keep server host as `127.0.0.1` and port as `6379` for a local server.
2. Choose a policy (LRU, LFU, FIFO, RANDOM, ADAPTIVE) and a maximum key count.
3. Enter tickers such as `AAPL, MSFT, NVDA` and select a history period.
4. Click **Fetch + cache in Redis**.
5. Inspect the chart, latest prices, and whether each `yf:<TICKER>:latest` record is still in Redis.

Each daily close is stored as `yf:<TICKER>:close:<YYYY-MM-DD>`; the latest record is stored as `yf:<TICKER>:latest`. Values are JSON strings. The UI sets `EVICTION` and `MAXKEYS` before writing data. If the key limit is smaller than the number of records, normal eviction may remove some stock records.

## Notes and limitations

- Yahoo Finance access requires an internet connection and depends on yfinance/Yahoo Finance availability.
- The chart shows the data fetched from Yahoo Finance; the Redis table independently checks whether each latest-price record survived in the C++ cache.
- The server's `MAXKEYS` setting counts unique keys, not bytes. One trading-day record is one key.
- The UI displays cache presence and configured policy/capacity. It does not invent hit/miss counters or benchmark claims because the server does not expose those counters through its current command interface.
- This is an educational stock-data demo, not investment advice.
