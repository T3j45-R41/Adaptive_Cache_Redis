#!/usr/bin/env python3
"""Benchmark client for Adaptive_Cache_Redis custom TCP server.

Supports dynamic server policy configuration (LRU, LFU, FIFO, RANDOM, CACHEUS),
capacity limits (CONFIG SET MAXKEYS), and queries internal server stats (INFO)
to measure real server-side cache evictions, hits, and misses.
"""
from __future__ import annotations
import argparse, concurrent.futures, csv, json, math, os, random, socket, statistics, time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
#Added latency calculation adjustments

def percentile(values: list[float], p: float) -> float:
    if not values: return 0.0
    ordered = sorted(values)
    return ordered[max(0, min(len(ordered)-1, math.ceil(p/100*len(ordered))-1))]


def command(host: str, port: int, text: str, timeout: float) -> tuple[bool, str, float]:
    start = time.perf_counter()
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall((text + "\r\n").encode("utf-8"))
            chunks = []
            while True:
                chunk = sock.recv(65536)
                if not chunk: break
                chunks.append(chunk)
                data = b"".join(chunks)
                if b"\r\n" in data:
                    if data.startswith(b"$") and not data.startswith(b"$-1"):
                        first = data.find(b"\r\n")
                        try: n = int(data[1:first])
                        except (ValueError, IndexError): break
                        if len(data) >= first + 2 + n + 2: break
                    else: break
            reply = b"".join(chunks).decode("utf-8", errors="replace")
            # Any RESP error starts with '-', whatever its error code. Matching
            # only "-Error"/"-ERR" would silently count a WRONGTYPE or
            # NOSUCHKEY reply as a successful operation.
            return bool(reply) and not reply.startswith("-"), reply, (time.perf_counter()-start)*1000
    except (OSError, socket.timeout) as exc:
        return False, f"{type(exc).__name__}: {exc}", (time.perf_counter()-start)*1000


def fetch_server_info(host: str, port: int, timeout: float) -> dict[str, Any]:
    ok, reply, _ = command(host, port, "INFO", timeout)
    info: dict[str, Any] = {}
    if ok and reply:
        for line in reply.splitlines():
            line = line.strip()
            if ":" in line and not line.startswith("#"):
                k, v = line.split(":", 1)
                k = k.strip()
                v = v.strip()
                try:
                    if "." in v: info[k] = float(v)
                    else: info[k] = int(v)
                except ValueError:
                    info[k] = v
    return info


def load_stock_csv(path: str) -> list[dict[str, str]]:
    """Load a yfinance-style CSV. Accepts Datetime/Date and Close, with optional Ticker/Symbol."""
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows: raise ValueError("Stock CSV is empty")
    fields = {str(k).strip().lower(): k for k in rows[0].keys() if k}
    time_col = next((fields[x] for x in ("datetime", "date", "timestamp") if x in fields), None)
    close_col = next((fields[x] for x in ("close", "adj close", "adj_close") if x in fields), None)
    ticker_col = next((fields[x] for x in ("ticker", "symbol") if x in fields), None)
    if not time_col or not close_col:
        raise ValueError("CSV must contain a Date/Datetime/Timestamp column and a Close/Adj Close column")
    result=[]
    for i,row in enumerate(rows):
        ts=str(row.get(time_col, "")).strip(); price=str(row.get(close_col, "")).strip()
        if not ts or not price or price.lower() in ("nan", "none"): continue
        ticker=(str(row.get(ticker_col, "STOCK")).strip() if ticker_col else "STOCK") or "STOCK"
        ticker="".join(c for c in ticker if c.isalnum() or c in "._-") or f"STOCK{i}"
        result.append({"ticker":ticker, "source_timestamp":ts.replace(" ", "T"), "price":price.replace(" ", "")})
    if not result: raise ValueError("No valid stock rows found in CSV")
    return result


def parse_timestamp(value: str) -> float | None:
    try:
        s=value.strip().replace("Z", "+00:00")
        dt=datetime.fromisoformat(s)
        if dt.tzinfo is None: dt=dt.replace(tzinfo=timezone.utc)
        return dt.timestamp()
    except (ValueError, OverflowError):
        return None


def make_workload(args: argparse.Namespace) -> tuple[list[dict[str, str]], float]:
    if args.stock_csv:
        records=load_stock_csv(args.stock_csv)
        latest=max((parse_timestamp(r["source_timestamp"]) for r in records if parse_timestamp(r["source_timestamp"]) is not None), default=None)
        for r in records:
            ts=parse_timestamp(r["source_timestamp"])
            r["staleness_seconds"] = max(0.0, latest-ts) if latest is not None and ts is not None else None
        return records, latest or 0.0
    now=datetime.now(timezone.utc).isoformat(timespec="seconds")
    records=[{"ticker":f"T{i:04d}", "source_timestamp":now, "price":f"{100+i/100:.2f}", "staleness_seconds":0.0} for i in range(args.keyspace)]
    return records, time.time()


def do_op(args: argparse.Namespace, worker: int, index: int, records: list[dict[str, Any]], rng_seed: int) -> dict[str, Any]:
    rng=random.Random(rng_seed)
    known = rng.random()*100 < args.hit_target_percent
    rec=records[rng.randrange(len(records))]
    key=f"stock:{rec['ticker']}" if args.stock_csv else f"bench:{rec['ticker']}"
    if known:
        get_ok, get_reply, get_ms = command(args.host,args.port,f"GET {key}",args.timeout)
        hit = get_ok and not get_reply.startswith("$-1")
        if not hit:
            value=f"{rec['price']}|{rec['source_timestamp']}"
            set_ok,set_reply,set_ms=command(args.host,args.port,f"SET {key} {value}",args.timeout)
            ok,reply,latency=get_ok and set_ok,get_reply if not set_ok else set_reply,get_ms+set_ms
            op="GET_MISS_THEN_SET"
        else:
            ok,reply,latency=get_ok,get_reply,get_ms; op="GET"
    else:
        key=f"bench:missing:{worker}:{index}:{rng_seed}"
        ok,reply,latency=command(args.host,args.port,f"GET {key}",args.timeout)
        hit=False; op="GET_COLD"
    staleness=rec.get("staleness_seconds")
    return {"timestamp":datetime.now(timezone.utc).isoformat(timespec="milliseconds"),"run_label":args.label,
      "host":args.host,"port":args.port,"concurrency":args.current_concurrency,"worker":worker,"operation_index":index,
      "operation":op,"key":key,"success":bool(ok),"cache_hit":bool(ok and hit and op!="GET_COLD"),
      "cache_miss":bool(ok and ((op=="GET_COLD" and reply.startswith("$-1")) or (op!="GET_COLD" and not hit))),"latency_ms":round(latency,4),"source_timestamp":rec["source_timestamp"],
      "source_data_staleness_seconds":round(float(staleness),3) if staleness is not None else "",
      "response":reply.replace("\r","\\r").replace("\n","\\n")[:300]}


def summarize(rows: list[dict[str, Any]], concurrency: int, wall: float, server_stats: dict[str, Any] | None = None) -> dict[str, Any]:
    lat=[float(r["latency_ms"]) for r in rows]; successes=sum(bool(r["success"]) for r in rows)
    hits=sum(bool(r["cache_hit"]) and bool(r["success"]) for r in rows)
    misses=sum(bool(r["cache_miss"]) for r in rows); errors=len(rows)-successes
    stale=[float(r["source_data_staleness_seconds"]) for r in rows if r["source_data_staleness_seconds"] not in ("",None)]
    
    server_evictions = server_stats.get("total_evictions") if server_stats else None
    server_policy = server_stats.get("active_policy") if server_stats else rows[0]["run_label"] if rows else "default"

    return {"run_label":rows[0]["run_label"] if rows else "default","concurrency":concurrency,"total_operations":len(rows),
      "success_count":successes,"error_count":errors,"error_rate_percent":round(errors/len(rows)*100,3) if rows else 0,
      "cache_hits_observed":hits,"cache_misses_observed":misses,"observed_hit_rate_percent":round(hits/(hits+misses)*100,3) if hits+misses else 0,
      "wall_time_seconds":round(wall,4),"throughput_ops_sec":round(successes/wall,3) if wall>0 else 0,
      "avg_latency_ms":round(statistics.mean(lat),4) if lat else 0,"median_latency_ms":round(statistics.median(lat),4) if lat else 0,
      "p95_latency_ms":round(percentile(lat,95),4),"p99_latency_ms":round(percentile(lat,99),4),
      "min_latency_ms":round(min(lat),4) if lat else 0,"max_latency_ms":round(max(lat),4) if lat else 0,
      "mean_source_data_staleness_seconds":round(statistics.mean(stale),3) if stale else None,
      "eviction_count":server_evictions,
      "server_policy":server_policy,
      "server_hits":server_stats.get("total_hits") if server_stats else None,
      "server_misses":server_stats.get("total_misses") if server_stats else None,
      "server_hit_rate_percent":server_stats.get("hit_rate_percent") if server_stats else None,
      "server_keys_stored":server_stats.get("keys_stored") if server_stats else None,
      "server_max_keys":server_stats.get("max_keys") if server_stats else None}


def main() -> int:
    p=argparse.ArgumentParser(description="Benchmark custom Redis-compatible TCP server and analyze observed metrics")
    p.add_argument("--host",default="127.0.0.1"); p.add_argument("--port",type=int,default=6379)
    p.add_argument("--operations",type=int,default=100); p.add_argument("--concurrency",type=int,nargs="+",default=[1])
    p.add_argument("--timeout",type=float,default=2.0); p.add_argument("--output-dir",default="results")
    p.add_argument("--stock-csv",help="Optional yfinance-style CSV with Date/Datetime, Close, and optional Ticker columns")
    p.add_argument("--keyspace",type=int,default=100,help="Synthetic stock-key count if --stock-csv is omitted")
    p.add_argument("--hit-target-percent",type=float,default=80.0,help="Percent of requests to target known stock keys (not a guaranteed hit rate)")
    p.add_argument("--label",default="custom-server",help="Label used to compare separate runs/policies")
    p.add_argument("--policy",help="Eviction policy to configure on server: LRU, LFU, FIFO, RANDOM, CACHEUS")
    p.add_argument("--max-keys",type=int,help="Cache capacity limit to configure on server (e.g. 50)")
    p.add_argument("--flush",action="store_true",default=True,help="Flush database before run (default True)")
    p.add_argument("--no-flush",dest="flush",action="store_false",help="Do not flush database before run")
    p.add_argument("--skip-ping",action="store_true")
    a=p.parse_args()

    if a.operations<1 or any(c<1 for c in a.concurrency) or a.timeout<=0 or a.keyspace<1 or not 0<=a.hit_target_percent<=100:
        p.error("operations/concurrency/keyspace/timeout must be positive and hit target must be 0..100")

    if not a.skip_ping:
        ok,reply,ms=command(a.host,a.port,"PING",a.timeout)
        if not ok or "PONG" not in reply:
            print(f"PING health check failed at {a.host}:{a.port}: {reply}"); return 2
        print(f"PING succeeded in {ms:.3f} ms")

    # Configure server policy and capacity if requested
    policy_to_set = a.policy or a.label
    if policy_to_set and policy_to_set.upper() in ("LRU", "LFU", "FIFO", "RANDOM", "CACHEUS", "ADAPTIVE"):
        ok,reply,_ = command(a.host, a.port, f"CONFIG SET POLICY {policy_to_set.upper()}", a.timeout)
        if ok: print(f"Server eviction policy configured: {policy_to_set.upper()}")
        else: print(f"Notice: CONFIG SET POLICY reply: {reply}")

    if a.max_keys:
        ok,reply,_ = command(a.host, a.port, f"CONFIG SET MAXKEYS {a.max_keys}", a.timeout)
        if ok: print(f"Server cache capacity configured: {a.max_keys} keys")
        else: print(f"Notice: CONFIG SET MAXKEYS reply: {reply}")

    if a.flush:
        ok,reply,_ = command(a.host, a.port, "FLUSHALL", a.timeout)
        if ok: print("Server database flushed (FLUSHALL)")

    command(a.host, a.port, "CONFIG RESETSTAT", a.timeout)

    try: records,_=make_workload(a)
    except (OSError,ValueError) as e: print(f"Workload error: {e}"); return 2

    # Seed known stock keys. This is setup, not included in measured operation count.
    for rec in records:
        key=f"stock:{rec['ticker']}" if a.stock_csv else f"bench:{rec['ticker']}"
        val=f"{rec['price']}|{rec['source_timestamp']}"
        ok,reply,ms=command(a.host,a.port,f"SET {key} {val}",a.timeout)
        if not ok: print(f"Seed failed for {key}: {reply}"); return 2

    os.makedirs(a.output_dir,exist_ok=True); stamp=datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    safe_label="".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in a.label)[:40] or "run"
    all_rows=[]; summaries=[]

    for c in a.concurrency:
        a.current_concurrency=c; print(f"Running {a.operations} GET operations at concurrency={c} ({a.label})...")
        start=time.perf_counter(); rows=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=c) as pool:
            futures=[pool.submit(do_op,a,i%c,i,records,(i+1)*7919+c*101) for i in range(a.operations)]
            for f in concurrent.futures.as_completed(futures): rows.append(f.result())
        wall=time.perf_counter()-start; rows.sort(key=lambda r:int(r["operation_index"]))

        server_stats = fetch_server_info(a.host, a.port, a.timeout)
        summary=summarize(rows,c,wall,server_stats); summaries.append(summary); all_rows.extend(rows)
        
        ev_str = f"evictions={summary['eviction_count']}" if summary['eviction_count'] is not None else "evictions=N/A"
        print(f"  success={summary['success_count']} hits={summary['cache_hits_observed']} misses={summary['cache_misses_observed']} {ev_str} throughput={summary['throughput_ops_sec']} ops/s p95={summary['p95_latency_ms']} ms")

    csv_path=os.path.join(a.output_dir,f"benchmark_{safe_label}_{stamp}.csv"); json_path=os.path.join(a.output_dir,f"summary_{safe_label}_{stamp}.json")
    with open(csv_path,"w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(all_rows[0].keys())); w.writeheader(); w.writerows(all_rows)

    final_info = fetch_server_info(a.host, a.port, a.timeout)
    config={"run_time":datetime.now(timezone.utc).isoformat(timespec="seconds"),"host":a.host,"port":a.port,"operations_per_concurrency":a.operations,
      "concurrency":a.concurrency,"timeout":a.timeout,"label":a.label,"policy":final_info.get("active_policy", a.label),
      "max_keys":final_info.get("max_keys", a.max_keys),"stock_csv":a.stock_csv,"hit_target_percent":a.hit_target_percent,
      "latency_scope":"end-to-end; fresh TCP connection per command","eviction_metrics_available":bool(final_info)}
    
    with open(json_path,"w",encoding="utf-8") as f: json.dump({"config":config,"server_stats":final_info,"summaries":summaries},f,indent=2)
    print("Saved raw CSV:",csv_path); print("Saved summary JSON:",json_path)
    if final_info:
        print(f"Server Stats -> Policy: {final_info.get('active_policy')}, Evictions: {final_info.get('total_evictions')}, Hits: {final_info.get('total_hits')}, Misses: {final_info.get('total_misses')}")
    print("Next: run python analyze_results.py --input",csv_path)
    return 0

if __name__=="__main__": raise SystemExit(main())
