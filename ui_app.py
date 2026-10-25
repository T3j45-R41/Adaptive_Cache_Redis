"""Control panel and live dashboard for the Adaptive_Cache_Redis server.

Run with:

    streamlit run ui_app.py

Start the server first, for example:

    cd Redis-Server && make && ./my_redis_server 6379 dump.my_rdb 300

The app talks to the server over RESP using :mod:`redis_client`. It works
against the custom C++ server only; the standard Redis GUI tools cannot be
used, because they open a connection with ``HELLO`` and expect RESP3 plus a
command surface (``COMMAND DOCS``, ``CLIENT LIST``, ``SCAN``) that this server
does not implement.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

import pandas as pd
import streamlit as st

from redis_client import POLICIES, RedisRESPClient, RedisRESPError

REPO_ROOT = Path(__file__).resolve().parent
DEFAULT_RESULTS = REPO_ROOT / "Benchmark" / "results"
DEFAULT_ANALYSIS = REPO_ROOT / "Benchmark" / "analysis_output"

HISTORY_LIMIT = 120

st.set_page_config(page_title="Adaptive Cache Redis", page_icon="🗄️", layout="wide")


# --------------------------------------------------------------------------
# Connection handling
# --------------------------------------------------------------------------

@st.cache_resource(show_spinner=False)
def _client(host: str, port: int) -> RedisRESPClient:
    """One client per (host, port), kept alive across Streamlit reruns."""
    return RedisRESPClient(host, port, timeout=3.0)


def client_for(host: str, port: int) -> RedisRESPClient:
    client = _client(host, port)
    if not client.connected:
        try:
            client.connect()
        except OSError:
            pass
    return client


def try_call(fn, *args, default=None, **kwargs):
    """Run a client call, reporting connection failures inline instead of crashing."""
    try:
        return fn(*args, **kwargs)
    except RedisRESPError as exc:
        st.error(f"Server rejected the command: {exc}")
        return default
    except OSError as exc:
        st.error(f"Cannot reach the server: {exc}")
        return default


def safe_info(client: RedisRESPClient) -> dict:
    try:
        return client.info()
    except (OSError, RedisRESPError):
        return {}


def require_server(client: RedisRESPClient) -> bool:
    """True when the server answers PING; otherwise shows the problem and returns False."""
    try:
        client.ping()
        return True
    except OSError as exc:
        st.error(
            f"Cannot reach {client.host}:{client.port} — {exc}\n\n"
            "Start the server first:\n\n"
            "```\ncd Redis-Server && make && ./my_redis_server "
            f"{client.port} dump.my_rdb 300\n```"
        )
        return False
    except RedisRESPError as exc:
        st.error(f"Unexpected reply to PING: {exc}")
        return False


def push_history(info: dict) -> None:
    history = st.session_state.setdefault("history", [])
    history.append(
        {
            "t": time.strftime("%H:%M:%S"),
            "keys": info.get("keys_stored", 0),
            "hit_rate": info.get("hit_rate_percent", 0),
            "evictions": info.get("total_evictions", 0),
            "hits": info.get("total_hits", 0),
            "misses": info.get("total_misses", 0),
        }
    )
    del history[:-HISTORY_LIMIT]


def history_frame() -> pd.DataFrame:
    history = st.session_state.get("history", [])
    return pd.DataFrame(history).set_index("t") if history else pd.DataFrame()


def fmt_bytes(value: int | float | None) -> str:
    if not value:
        return "0 B"
    size = float(value)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:,.1f} {unit}" if unit != "B" else f"{int(size)} B"
        size /= 1024
    return f"{size:.1f} GB"


# --------------------------------------------------------------------------
# Live dashboard
# --------------------------------------------------------------------------

def render_dashboard_body(client: RedisRESPClient) -> None:
    info = safe_info(client)
    if not info:
        st.info("Waiting for the server…")
        return

    push_history(info)

    limit = int(info.get("max_keys", 0))
    stored = int(info.get("keys_stored", 0))
    ratio = (stored / limit) if limit else 0.0

    top = st.columns(6)
    top[0].metric("Keys stored", stored, help="Resident keys in the cache")
    top[1].metric(
        "Capacity",
        limit or "∞",
        help="Maximum keys before eviction kicks in (0 means unlimited)",
    )
    top[2].metric("Hit rate", f"{info.get('hit_rate_percent', 0)}%")
    top[3].metric("Hits", f"{int(info.get('total_hits', 0)):,}")
    top[4].metric("Misses", f"{int(info.get('total_misses', 0)):,}")
    top[5].metric(
        "Evictions",
        f"{int(info.get('total_evictions', 0)):,}",
        help="Keys discarded to stay within the capacity limit",
    )

    if limit:
        st.progress(
            min(ratio, 1.0),
            text=f"Cache {stored}/{limit} keys — {ratio * 100:.0f}% full",
        )

    second = st.columns(5)
    second[0].metric("Policy", info.get("active_policy", "?"))
    second[1].metric("Bytes used", fmt_bytes(info.get("bytes_used")))
    second[2].metric("Write-backs", f"{int(info.get('write_backs', 0)):,}",
                     help="Evictions of modified frames, i.e. the I/O cost")
    second[3].metric("Accesses", f"{int(info.get('total_accesses', 0)):,}")
    second[4].metric("Expirations", f"{int(info.get('expirations', 0)):,}")

    frame = history_frame()
    if not frame.empty:
        st.subheader("Live history")
        left, right = st.columns(2)
        with left:
            st.line_chart(frame[["hit_rate"]], height=200)
            st.caption("Server-side hit rate (%)")
        with right:
            st.line_chart(frame[["keys", "evictions"]], height=200)
            st.caption("Resident keys and cumulative evictions")

    counts = {
        "LRU": info.get("lru_selections", 0),
        "LFU": info.get("lfu_selections", 0),
        "FIFO": info.get("fifo_selections", 0),
        "Random": info.get("random_selections", 0),
        "Adaptive": info.get("adaptive_selections", 0),
    }
    used = {name: value for name, value in counts.items() if value}
    if used:
        st.subheader("Victim selection by policy")
        st.bar_chart(pd.DataFrame({"evictions": used}))
        st.caption(
            "Only the active policy can nominate victims, so at most one of these "
            "bars is non-zero at a time."
        )

    detail = str(info.get("adaptive_detail", ""))
    if detail:
        with st.expander("Adaptive controller state"):
            for field in detail.split():
                key, _, value = field.partition("=")
                if value:
                    st.code(f"{key:>18} = {value}", language=None)


# --------------------------------------------------------------------------
# Tab: Control
# --------------------------------------------------------------------------

def tab_control(client: RedisRESPClient) -> None:
    st.subheader("Cache control")

    left, right = st.columns(2)

    with left:
        st.markdown("#### Replacement policy")
        info = safe_info(client)
        current = str(info.get("active_policy", "ADAPTIVE"))
        index = POLICIES.index(current) if current in POLICIES else 0
        policy = st.selectbox("Policy", POLICIES, index=index,
                              help="Switching hands the resident key set to the new policy")
        if st.button("Apply policy", width="stretch",
                     disabled=policy == current):
            if try_call(client.set_policy, policy) == "OK":
                st.success(f"Policy set to {policy}.")
                st.rerun()

        with st.expander("What each policy does"):
            st.markdown(
                "- **LRU** — evicts the least recently used key.\n"
                "- **LFU** — evicts the least frequently used key.\n"
                "- **FIFO** — evicts in insertion order, ignoring reads.\n"
                "- **RANDOM** — a uniformly random resident key; the baseline.\n"
                "- **ADAPTIVE** — LRU and LFU experts scored in the shadow, with "
                "weights updated against the worse performer."
            )

    with right:
        st.markdown("#### Capacity")
        limit = int(info.get("max_keys", 0))
        new_limit = st.number_input("Maximum keys (0 = unlimited)", min_value=0,
                                    max_value=1_000_000, value=limit or 100, step=1)
        st.caption(
            f"Currently **{limit if limit else 'unlimited'}**. "
            "Lowering the limit evicts immediately until the cache fits."
        )
        if st.button("Apply capacity", width="stretch",
                     disabled=new_limit == limit):
            if try_call(client.set_maxkeys, new_limit) == "OK":
                st.success(f"Capacity set to {new_limit if new_limit else 'unlimited'}.")
                st.rerun()

    st.divider()
    st.markdown("#### Maintenance")
    m1, m2 = st.columns(2)
    with m1:
        if st.button("Reset statistics", width="stretch",
                     help="Zeroes hit, miss and eviction counters"):
            if try_call(client.reset_stats) == "OK":
                st.success("Counters reset.")
                st.rerun()
    with m2:
        confirm = st.checkbox("I understand this deletes every key")
        if st.button("FLUSHALL", width="stretch", disabled=not confirm):
            if try_call(client.flushall) == "OK":
                st.success("Cache cleared.")
                st.rerun()


# --------------------------------------------------------------------------
# Tab: Key explorer
# --------------------------------------------------------------------------

def tab_keys(client: RedisRESPClient) -> None:
    st.subheader("Key explorer")

    bar1, bar2, bar3 = st.columns([3, 1, 1])
    pattern = bar1.text_input("Pattern", value="*",
                              help="Glob: * any run, ? one character, e.g. stock:*")
    limit = bar2.number_input("Max keys", min_value=10, max_value=500, value=100, step=10)
    search = bar3.button("Search", width="stretch")

    if search or "key_rows" in st.session_state:
        rows = st.session_state.get("key_rows")
        if search:
            keys = try_call(client.keys, pattern, default=[]) or []
            rows = client.describe_many(keys, limit=int(limit))
            st.session_state["key_rows"] = rows
            st.session_state["key_pattern"] = pattern

        if not rows:
            st.info("No keys matched.")
            return

        st.caption(f"{len(rows)} keys described (each costs three round trips).")
        frame = pd.DataFrame(rows)

        counts = frame["type"].value_counts().to_dict()
        st.write(" · ".join(f"**{count}** {kind}" for kind, count in counts.items()))

        frame["ttl_label"] = frame["ttl"].map(
            lambda value: "—" if value == -1 else ("missing" if value == -2 else f"{value}s")
        )
        st.dataframe(
            frame[["key", "type", "ttl_label", "bytes"]].rename(
                columns={"ttl_label": "ttl"}
            ),
            width="stretch",
            hide_index=True,
        )

        selected = st.selectbox("Inspect key", frame["key"].tolist())
        if selected:
            show_key_detail(client, selected)


def show_key_detail(client: RedisRESPClient, key: str) -> None:
    st.markdown(f"#### `{key}`")
    try:
        kind = client.key_type(key)
    except (OSError, RedisRESPError) as exc:
        st.error(str(exc))
        return

    if kind == "none":
        st.warning("This key is no longer resident — it was evicted, expired or deleted.")
        return

    ttl = try_call(client.ttl, key, default=-2)
    st.caption(
        f"type: **{kind}** · "
        f"ttl: **{'no expiry' if ttl == -1 else ttl}** · "
        f"size: **{fmt_bytes(client.memory_usage(key))}**"
    )

    if kind == "string":
        current = try_call(client.get, key, default="") or ""
        edited = st.text_area("Value", value=current, height=140, key=f"edit_{key}")
        b1, b2 = st.columns(2)
        with b1:
            if st.button("Save", width="stretch", key=f"save_{key}"):
                if try_call(client.set, key, edited) == "OK":
                    st.success("Saved.")
                    st.rerun()
        with b2:
            if st.button("Delete", width="stretch", key=f"del_{key}"):
                client.delete(key)
                st.success(f"Deleted `{key}`.")
                st.session_state.pop("key_rows", None)
                st.rerun()

    elif kind == "list":
        items = try_call(client.lget, key, default=[]) or []
        st.dataframe(pd.DataFrame({"index": range(len(items)), "item": items}),
                     width="stretch", hide_index=True)

    elif kind == "hash":
        fields = try_call(client.hgetall, key, default={}) or {}
        frame = pd.DataFrame(
            [{"field": f, "value": v} for f, v in sorted(fields.items())]
        )
        st.dataframe(frame, width="stretch", hide_index=True)


# --------------------------------------------------------------------------
# Tab: Eviction lab
# --------------------------------------------------------------------------

def render_eviction_body(client: RedisRESPClient) -> None:
    info = safe_info(client)
    limit = int(info.get("max_keys", 0))
    stored = int(info.get("keys_stored", 0))

    top = st.columns(4)
    top[0].metric("Policy", info.get("active_policy", "?"))
    top[1].metric("Capacity", limit or "unlimited")
    top[2].metric("Keys resident", stored)
    top[3].metric("Evictions", f"{int(info.get('total_evictions', 0)):,}")

    if limit:
        st.progress(min(stored / limit, 1.0), text=f"{stored}/{limit} keys resident")

    keys = try_call(client.keys, "evict:*", default=[]) or []
    if keys:
        st.write("Keys currently resident:")
        st.code("\n".join(sorted(keys)[:40]), language=None)


def tab_eviction(client: RedisRESPClient) -> None:
    st.subheader("Eviction lab")
    st.caption(
        "Set a small capacity, fill the cache past it, and watch the server discard "
        "keys to stay within the limit."
    )

    setup = st.columns([1, 1, 2])
    capacity = setup[0].number_input("Capacity", min_value=2, max_value=100, value=8, step=1)
    overflow = setup[1].number_input("Keys to write", min_value=3, max_value=200,
                                    value=12, step=1)
    watch_key = setup[2].text_input("Key to track", value="evict:00")

    run1, run2, run3 = st.columns(3)
    if run1.button("Set capacity", width="stretch"):
        if try_call(client.set_maxkeys, capacity) == "OK":
            st.session_state.pop("evict_watch", None)
            st.success(f"Capacity set to {capacity}.")
            st.rerun()

    if run2.button("Write keys (overflow)", width="stretch"):
        written = 0
        for index in range(int(overflow)):
            key = f"evict:{index:02d}"
            try:
                client.set(key, f"value-{index}")
                written += 1
            except (OSError, RedisRESPError):
                break
        st.session_state["evict_watch"] = watch_key
        st.success(f"Wrote {written} keys into a cache of capacity {capacity}.")
        st.rerun()

    if run3.button("Reset counters", width="stretch"):
        try_call(client.reset_stats)
        st.session_state.pop("evict_watch", None)
        st.rerun()

    st.divider()
    watched = st.session_state.get("evict_watch")
    if watched:
        try:
            alive = client.exists(watched)
        except (OSError, RedisRESPError):
            alive = False
        if alive:
            st.success(f"`{watched}` is still resident.")
        else:
            st.error(f"`{watched}` has been evicted.")

    live = st.fragment(run_every=1.0)(render_eviction_body)
    live(client=client)


# --------------------------------------------------------------------------
# Tab: Benchmarks
# --------------------------------------------------------------------------

def aggregate_raw_results(results_dir: Path) -> pd.DataFrame | None:
    """Build a comparison table straight from the raw per-operation CSVs."""
    files = sorted(results_dir.glob("benchmark_*.csv"))
    if not files:
        return None
    frames = []
    for path in files:
        try:
            frame = pd.read_csv(path)
        except Exception:
            continue
        if "run_label" not in frame.columns:
            continue
        frames.append(frame)
    if not frames:
        return None

    data = pd.concat(frames, ignore_index=True)
    rows = []
    for (label, concurrency), group in data.groupby(["run_label", "concurrency"]):
        latency = _numeric(group, "latency_ms").dropna()
        hits = _numeric(group, "cache_hit").fillna(0).astype(bool).sum()
        misses = _numeric(group, "cache_miss").fillna(0).astype(bool).sum()
        total = hits + misses
        rows.append(
            {
                "run_label": label,
                "concurrency": int(concurrency),
                "operations": len(group),
                "hit_rate_percent": round(100 * hits / total, 2) if total else 0.0,
                "median_latency_ms": round(latency.median(), 4) if not latency.empty else 0.0,
                "p95_latency_ms": round(latency.quantile(0.95), 4) if not latency.empty else 0.0,
            }
        )
    return pd.DataFrame(rows)


def _numeric(group: pd.DataFrame, column: str) -> pd.Series:
    """Numeric view of a column, or an empty series when it is absent."""
    if column not in group.columns:
        return pd.Series(dtype="float64")
    return pd.to_numeric(group[column], errors="coerce")


def tab_benchmarks(_client: RedisRESPClient) -> None:
    st.subheader("Benchmark results")
    st.caption(
        "Populate this by running the benchmark client from the `Benchmark` folder, "
        "then `analyze_results.py`."
    )

    cols = st.columns(2)
    results_dir = Path(
        cols[0].text_input("Results folder", value=str(DEFAULT_RESULTS))
    ).expanduser()
    analysis_dir = Path(
        cols[1].text_input("Analysis folder (optional)", value=str(DEFAULT_ANALYSIS))
    ).expanduser()

    summary = analysis_dir / "analysis_summary.csv"
    frame: pd.DataFrame | None = None
    source = ""

    if summary.exists():
        try:
            frame = pd.read_csv(summary)
            source = str(summary)
        except Exception as exc:
            st.error(f"Could not read {summary}: {exc}")

    if frame is None:
        frame = aggregate_raw_results(results_dir)
        if frame is not None:
            source = str(results_dir)
            st.info(
                "Showing an aggregate computed directly from the raw CSVs. "
                "Run `analyze_results.py` for the full charts."
            )

    if frame is None or frame.empty:
        st.warning("No benchmark results found yet.")
        st.code(
            "cd Benchmark\n"
            "python generate_stock_workload.py\n"
            "python benchmark_client.py --policy ADAPTIVE --label ADAPTIVE "
            "--max-keys 25 --operations 300 --concurrency 1 5\n"
            "python analyze_results.py --input results",
            language="bash",
        )
        return

    st.caption(f"Source: `{source}` · {len(frame)} rows")
    st.dataframe(frame, width="stretch", hide_index=True)

    label_col = "run_label" if "run_label" in frame.columns else None
    if not label_col:
        return

    for metric, title in [
        ("hit_rate_percent", "Hit rate by policy"),
        ("server_eviction_count", "Evictions by policy"),
        ("p95_latency_ms", "P95 latency by policy"),
    ]:
        if metric not in frame.columns:
            continue
        plot = frame.pivot_table(
            index=label_col, columns="concurrency", values=metric, aggfunc="mean"
        )
        with st.expander(title, expanded=metric == "hit_rate_percent"):
            st.bar_chart(plot)

    if "server_eviction_count" in frame.columns and frame["server_eviction_count"].isna().all():
        st.warning(
            "No server-side eviction counts were recorded. This happens if the run "
            "predates `INFO` support, or if `CONFIG RESETSTAT` ran after the "
            "measured operations."
        )


# --------------------------------------------------------------------------
# Tab: Stock data (yfinance)
# --------------------------------------------------------------------------

@st.cache_data(ttl=600, show_spinner=False)
def fetch_history(ticker: str, period: str) -> pd.DataFrame:
    import yfinance as yf

    frame = yf.Ticker(ticker).history(period=period, interval="1d", auto_adjust=True)
    if frame.empty:
        return pd.DataFrame()
    out = pd.DataFrame(
        {
            "Date": [d.strftime("%Y-%m-%d") for d in frame.index],
            "Close": frame["Close"].round(4).tolist(),
            "Volume": frame["Volume"].astype("int64").tolist(),
        }
    )
    return out


def cache_history(client: RedisRESPClient, ticker: str, frame: pd.DataFrame) -> int:
    writes = 0
    for row in frame.to_dict("records"):
        client.set(f"yf:{ticker}:close:{row['Date']}", json.dumps(row, separators=(",", ":")))
        writes += 1
    latest = frame.iloc[-1].to_dict()
    client.set(f"yf:{ticker}:latest", json.dumps(latest, separators=(",", ":")))
    return writes + 1


def read_cached_latest(client: RedisRESPClient, ticker: str) -> dict | None:
    raw = client.get(f"yf:{ticker}:latest")
    return json.loads(raw) if raw else None


def tab_stock(client: RedisRESPClient) -> None:
    st.subheader("Cache real stock quotes")
    st.caption(
        "Fetches daily closes from Yahoo Finance and writes them into the C++ "
        "server, so you can watch the cache evict real records under a small "
        "capacity limit."
    )

    cols = st.columns([2, 1, 1])
    ticker_text = cols[0].text_input("Tickers (comma separated)", value="AAPL, MSFT, NVDA")
    period = cols[1].selectbox("Period", ["1mo", "3mo", "6mo", "1y", "2y"], index=1)
    load = cols[2].button("Fetch + cache", type="primary", width="stretch")

    if load:
        tickers = list(dict.fromkeys(
            t.strip().upper() for t in ticker_text.split(",") if t.strip()
        ))
        if not tickers:
            st.error("Enter at least one ticker.")
            return

        frames, failures, writes = {}, [], 0
        progress = st.progress(0.0, text="Contacting Yahoo Finance…")
        for index, ticker in enumerate(tickers):
            try:
                frame = fetch_history(ticker, period)
                if frame.empty:
                    failures.append(f"{ticker}: no history returned")
                else:
                    frames[ticker] = frame
                    writes += cache_history(client, ticker, frame)
            except Exception as exc:
                failures.append(f"{ticker}: {exc}")
            progress.progress((index + 1) / len(tickers), text=f"Processed {ticker}")
        progress.empty()

        st.session_state["yf_frames"] = frames
        st.session_state["yf_tickers"] = tickers
        st.session_state["yf_writes"] = writes
        st.success(f"Stored {writes} records for {len(frames)} ticker(s).")
        for failure in failures:
            st.warning(failure)

    frames = st.session_state.get("yf_frames")
    if not frames:
        return

    left, right = st.columns([1.25, 1])
    with left:
        ticker = st.selectbox("Chart ticker", list(frames.keys()))
        st.line_chart(frames[ticker].set_index("Date")["Close"], height=320)
    with right:
        rows = []
        for name, frame in frames.items():
            last = frame.iloc[-1]
            rows.append({
                "Ticker": name, "Date": last["Date"],
                "Close": float(last["Close"]), "Volume": int(last["Volume"]),
            })
        st.dataframe(pd.DataFrame(rows), hide_index=True, width="stretch")

    st.markdown("#### Survival check")
    rows = []
    for name in frames:
        cached = read_cached_latest(client, name)
        rows.append({
            "Ticker": name,
            "Cached date": cached["date"] if cached else "—",
            "Cached close": cached["close"] if cached else "—",
            "Redis key": f"yf:{name}:latest",
            "Status": "Resident" if cached else "Evicted / expired",
        })
    st.dataframe(pd.DataFrame(rows), hide_index=True, width="stretch")


# --------------------------------------------------------------------------
# Sidebar and layout
# --------------------------------------------------------------------------

def main() -> None:
    st.title("Adaptive Cache Redis")
    st.caption("Control panel and live dashboard for the C++ cache server")

    with st.sidebar:
        st.header("Server")
        host = st.text_input("Host", value="127.0.0.1").strip() or "127.0.0.1"
        port = int(st.number_input("Port", min_value=1, max_value=65535, value=6379, step=1))

        if st.button("Connect", width="stretch"):
            _client.clear()
            st.rerun()

        client = client_for(host, port)

        if st.session_state.get("connected_to") != (host, port):
            st.session_state["connected_to"] = (host, port)
            st.session_state.pop("history", None)

        online = require_server(client)
        if online:
            st.success(
                f"Connected · policy **{safe_info(client).get('active_policy', '?')}**"
            )
        else:
            st.warning("Not connected")

        st.divider()
        if st.button("Clear live history", width="stretch"):
            st.session_state.pop("history", None)
            st.rerun()

    if not online:
        return

    auto = st.sidebar.checkbox("Auto-refresh dashboard", value=True, key="auto_refresh")
    st.sidebar.caption("Refreshes the Dashboard tab every two seconds.")

    dashboard, control, explorer, lab, benchmarks, stock = st.tabs(
        ["Dashboard", "Control", "Keys", "Eviction lab", "Benchmarks", "Stock data"]
    )

    with dashboard:
        interval = 2.0 if auto else None
        st.fragment(run_every=interval)(render_dashboard_body)(client=client)
        if not auto:
            st.button("Refresh", on_click=st.rerun)

    with control:
        tab_control(client)
    with explorer:
        tab_keys(client)
    with lab:
        tab_eviction(client)
    with benchmarks:
        tab_benchmarks(client)
    with stock:
        tab_stock(client)


if __name__ == "__main__":
    main()