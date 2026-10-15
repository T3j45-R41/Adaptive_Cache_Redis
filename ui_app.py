"""Small Streamlit UI: Yahoo Finance history + the project's Redis-compatible cache."""
from __future__ import annotations

import json
import socket
import time
from datetime import date
from typing import Any, BinaryIO

import pandas as pd
import streamlit as st
import yfinance as yf


class RedisRESPClient:
    """Minimal RESP2 client for the project's TCP server (one command per connection)."""

    def __init__(self, host: str = "127.0.0.1", port: int = 6379, timeout: float = 3.0):
        self.host = host
        self.port = int(port)
        self.timeout = timeout

    @staticmethod
    def _encode_command(parts: tuple[Any, ...]) -> bytes:
        encoded = [str(part).encode("utf-8") for part in parts]
        request = [f"*{len(encoded)}\r\n".encode("ascii")]
        for item in encoded:
            request.append(f"${len(item)}\r\n".encode("ascii"))
            request.append(item + b"\r\n")
        return b"".join(request)

    @staticmethod
    def _read_line(stream: BinaryIO) -> bytes:
        line = stream.readline()
        if not line or not line.endswith(b"\r\n"):
            raise ConnectionError("Server closed the connection or returned an incomplete RESP line.")
        return line[:-2]

    @classmethod
    def _read_response(cls, stream: BinaryIO) -> Any:
        line = cls._read_line(stream)
        if not line:
            raise ConnectionError("Empty response from Redis server.")
        prefix, payload = line[:1], line[1:]
        if prefix == b"+":
            return payload.decode("utf-8", errors="replace")
        if prefix == b"-":
            raise RuntimeError(payload.decode("utf-8", errors="replace"))
        if prefix == b":":
            return int(payload)
        if prefix == b"$":
            length = int(payload)
            if length == -1:
                return None
            data = stream.read(length)
            ending = stream.read(2)
            if len(data) != length or ending != b"\r\n":
                raise ConnectionError("Incomplete bulk-string response from server.")
            return data.decode("utf-8", errors="replace")
        if prefix == b"*":
            count = int(payload)
            if count == -1:
                return None
            return [cls._read_response(stream) for _ in range(count)]
        raise ConnectionError(f"Unsupported RESP response prefix: {prefix!r}")

    def command(self, *parts: Any) -> Any:
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(self._encode_command(parts))
            with sock.makefile("rb") as stream:
                return self._read_response(stream)


def fetch_history(ticker: str, period: str) -> pd.DataFrame:
    """Fetch Yahoo Finance daily history; cache briefly in the UI process."""
    history = yf.Ticker(ticker).history(period=period, interval="1d", auto_adjust=True)
    if history is None or history.empty or "Close" not in history:
        return pd.DataFrame()
    frame = history.reset_index()
    date_column = "Date" if "Date" in frame.columns else frame.columns[0]
    frame["Date"] = pd.to_datetime(frame[date_column], errors="coerce", utc=True).dt.strftime("%Y-%m-%d")
    frame["Close"] = pd.to_numeric(frame["Close"], errors="coerce")
    if "Volume" not in frame.columns:
        frame["Volume"] = 0
    frame["Volume"] = pd.to_numeric(frame["Volume"], errors="coerce").fillna(0).astype("int64")
    return frame[["Date", "Close", "Volume"]].dropna(subset=["Date", "Close"]).reset_index(drop=True)


def cache_history(client: RedisRESPClient, ticker: str, frame: pd.DataFrame) -> int:
    """Store one key per trading day plus a latest-price pointer."""
    writes = 0
    latest = None
    for row in frame.itertuples(index=False):
        item = {"ticker": ticker, "date": str(row.Date), "close": round(float(row.Close), 6), "volume": int(row.Volume)}
        client.command("SET", f"yf:{ticker}:close:{item['date']}", json.dumps(item, separators=(",", ":")))
        latest = item
        writes += 1
    if latest is not None:
        client.command("SET", f"yf:{ticker}:latest", json.dumps(latest, separators=(",", ":")))
        writes += 1
    return writes


def read_cached_latest(client: RedisRESPClient, ticker: str) -> dict[str, Any] | None:
    value = client.command("GET", f"yf:{ticker}:latest")
    if value is None:
        return None
    try:
        parsed = json.loads(value)
        return parsed if isinstance(parsed, dict) else None
    except (json.JSONDecodeError, TypeError):
        return None


st.set_page_config(page_title="Adaptive Redis Stock Cache", page_icon="📈", layout="wide")
st.title("📈 Adaptive Cache Redis — Yahoo Finance Demo")
st.caption("Yahoo Finance provides the market history; your C++ Redis-compatible server stores and evicts the cached records.")

with st.sidebar:
    st.header("Server and cache")
    host = st.text_input("Server host", value="127.0.0.1")
    port = st.number_input("Server port", min_value=1, max_value=65535, value=6379, step=1)
    policy = st.selectbox("Eviction policy", ["LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"])
    capacity = st.number_input("Maximum keys", min_value=1, max_value=100000, value=1000, step=1)
    check_server = st.button("Check server connection", use_container_width=True)

client = RedisRESPClient(host.strip(), int(port))
if check_server:
    try:
        pong = client.command("PING")
        active_policy = client.command("EVICTION")
        max_keys = client.command("MAXKEYS")
        st.sidebar.success(f"Connected: {pong}. Policy: {active_policy}. Capacity: {max_keys} keys.")
    except Exception as exc:
        st.sidebar.error(f"Could not connect: {exc}")

st.subheader("1. Fetch stock data")
col1, col2, col3 = st.columns([2, 1, 1])
with col1:
    ticker_text = st.text_input("Ticker symbols (comma-separated)", value="AAPL, MSFT, NVDA")
with col2:
    period = st.selectbox("History period", ["1mo", "3mo", "6mo", "1y", "2y"], index=1)
with col3:
    st.write("")
    st.write("")
    load_button = st.button("Fetch + cache in Redis", type="primary", use_container_width=True)

if load_button:
    tickers = list(dict.fromkeys(t.strip().upper() for t in ticker_text.split(",") if t.strip()))
    if not tickers:
        st.error("Enter at least one ticker symbol.")
    else:
        try:
            # Apply requested cache settings before writing stock records.
            client.command("EVICTION", policy)
            client.command("MAXKEYS", int(capacity))
            combined: dict[str, pd.DataFrame] = {}
            total_writes = 0
            failures: list[str] = []
            progress = st.progress(0, text="Contacting Yahoo Finance…")
            for idx, ticker in enumerate(tickers):
                try:
                    frame = fetch_history(ticker, period)
                    if frame.empty:
                        failures.append(f"{ticker}: no price history returned")
                    else:
                        combined[ticker] = frame
                        total_writes += cache_history(client, ticker, frame)
                except Exception as exc:
                    failures.append(f"{ticker}: {exc}")
                progress.progress((idx + 1) / len(tickers), text=f"Processed {ticker}")
            progress.empty()
            st.session_state["yf_stock_frames"] = combined
            st.session_state["yf_stock_tickers"] = tickers
            st.session_state["yf_stock_policy"] = policy
            st.session_state["yf_stock_capacity"] = int(capacity)
            st.session_state["yf_stock_writes"] = total_writes
            st.session_state["yf_stock_failures"] = failures
            st.success(f"Finished. Stored {total_writes} records/updates for {len(combined)} ticker(s).")
            if failures:
                for failure in failures:
                    st.warning(failure)
        except Exception as exc:
            st.error(f"Could not write to the cache server: {exc}")

frames = st.session_state.get("yf_stock_frames", {})
if frames:
    st.subheader("2. Price history")
    chart_col, table_col = st.columns([1.25, 1])
    with chart_col:
        selected_ticker = st.selectbox("Chart ticker", list(frames.keys()))
        chart_frame = frames[selected_ticker].set_index("Date")
        st.line_chart(chart_frame["Close"], height=320)
        st.caption("Adjusted daily close prices returned by yfinance. The chart is drawn from the fetched history.")
    with table_col:
        st.markdown("**Latest fetched prices**")
        summary_rows = []
        for ticker, frame in frames.items():
            last = frame.iloc[-1]
            summary_rows.append({"Ticker": ticker, "Date": last["Date"], "Close": float(last["Close"]), "Volume": int(last["Volume"])})
        st.dataframe(pd.DataFrame(summary_rows), hide_index=True, use_container_width=True)

    st.subheader("3. Verify values in your C++ cache")
    try:
        server_policy = client.command("EVICTION")
        server_capacity = client.command("MAXKEYS")
        all_keys = client.command("KEYS", "*")
        all_keys = all_keys or []
        st.caption(f"Active policy: {server_policy} · Maximum keys: {server_capacity} · Current keys: {len(all_keys)}")
        cached_rows = []
        for ticker in frames:
            cached = read_cached_latest(client, ticker)
            if cached:
                cached_rows.append({"Ticker": ticker, "Cached date": cached.get("date"), "Cached close": cached.get("close"), "Redis key": f"yf:{ticker}:latest", "Status": "Present"})
            else:
                cached_rows.append({"Ticker": ticker, "Cached date": "—", "Cached close": "—", "Redis key": f"yf:{ticker}:latest", "Status": "Evicted / missing"})
        st.dataframe(pd.DataFrame(cached_rows), hide_index=True, use_container_width=True)
        st.caption("A latest-price key may be evicted if the configured key capacity is too small. This demo shows cache presence, not server-side hit-rate counters.")
        with st.expander("Inspect stock-related keys currently in Redis"):
            stock_keys = [key for key in all_keys if isinstance(key, str) and key.startswith("yf:")]
            st.write(f"{len(stock_keys)} stock-related keys found")
            st.code("\n".join(stock_keys[:200]) if stock_keys else "No stock keys currently present")
            if len(stock_keys) > 200:
                st.caption("Showing first 200 stock keys.")
    except Exception as exc:
        st.error(f"Unable to inspect cache state: {exc}")
else:
    st.info("Start your C++ server, then click **Fetch + cache in Redis** to load stock history and display it here.")

st.divider()
st.caption("Educational project demo only. Yahoo Finance data availability and delays may vary. This UI is not investment advice.")
