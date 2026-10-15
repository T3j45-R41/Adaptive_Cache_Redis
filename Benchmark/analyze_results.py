#!/usr/bin/env python3
"""Pandas/Matplotlib analysis for benchmark CSV output. Install requirements.txt first."""
import argparse, os, sys, json
import pandas as pd
#Script handles parseing csv logs and output visual comparision charts
def main():
    p=argparse.ArgumentParser(description="Analyze benchmark CSV and create charts/report")
    p.add_argument("--input",required=True,help="benchmark_*.csv file, or a directory containing benchmark CSV files")
    p.add_argument("--output-dir",default="analysis_output")
    a=p.parse_args(); os.makedirs(a.output_dir,exist_ok=True)
    
    input_dir = a.input if os.path.isdir(a.input) else os.path.dirname(a.input) or "."
    if os.path.isdir(a.input):
        files=[os.path.join(a.input,x) for x in os.listdir(a.input) if x.startswith("benchmark_") and x.endswith(".csv")]
    else:
        files=[a.input]
    if not files: print("No benchmark CSV files found."); return 2
    
    frames=[]
    for f in files:
        d=pd.read_csv(f); d["source_file"]=os.path.basename(f); frames.append(d)
    df=pd.concat(frames,ignore_index=True)
    required={"run_label","concurrency","latency_ms","success","cache_hit","cache_miss"}
    missing=required-set(df.columns)
    if missing: print("CSV missing required columns:",", ".join(sorted(missing))); return 2
    
    df["success"]=df["success"].astype(str).str.lower().isin(["true","1","yes"])
    df["cache_hit"]=df["cache_hit"].astype(str).str.lower().isin(["true","1","yes"])
    df["cache_miss"]=df["cache_miss"].astype(str).str.lower().isin(["true","1","yes"])
    
    group=df.groupby(["run_label","concurrency"],dropna=False)
    summary=group.agg(
        total_operations=("latency_ms","count"),
        success_count=("success","sum"),
        avg_latency_ms=("latency_ms","mean"),
        median_latency_ms=("latency_ms","median"),
        p95_latency_ms=("latency_ms",lambda x:x.quantile(.95)),
        p99_latency_ms=("latency_ms",lambda x:x.quantile(.99)),
        hits=("cache_hit","sum"),
        misses=("cache_miss","sum")
    ).reset_index()
    summary["observed_hit_rate_percent"]=100*summary["hits"]/(summary["hits"]+summary["misses"]).replace(0,float("nan"))
    
    # Try to extract real server eviction counts from matching summary_*.json files
    json_files = [os.path.join(input_dir, x) for x in os.listdir(input_dir) if x.startswith("summary_") and x.endswith(".json")]
    json_rows = []
    for jf in json_files:
        try:
            with open(jf, "r", encoding="utf-8") as f:
                jdata = json.load(f)
            for s in jdata.get("summaries", []):
                lbl = s.get("run_label")
                c = s.get("concurrency")
                ev = s.get("eviction_count")
                if lbl is not None and c is not None and ev is not None:
                    json_rows.append({"run_label": lbl, "concurrency": c, "server_eviction_count": ev})
        except Exception:
            pass
            
    if json_rows:
        jdf = pd.DataFrame(json_rows).drop_duplicates(subset=["run_label", "concurrency"])
        summary = summary.merge(jdf, on=["run_label", "concurrency"], how="left")

    # Approximate throughput using elapsed time between first and last recorded operation timestamps.
    if "timestamp" in df.columns:
        df["timestamp_parsed"]=pd.to_datetime(df["timestamp"],errors="coerce",utc=True)
        spans=df.groupby(["run_label","concurrency"])["timestamp_parsed"].agg(lambda x:(x.max()-x.min()).total_seconds() if x.notna().sum()>1 else float("nan")).reset_index(name="observed_timestamp_span_seconds")
        summary=summary.merge(spans,on=["run_label","concurrency"],how="left")
        summary["approx_ops_per_second"]=summary["success_count"]/summary["observed_timestamp_span_seconds"].replace(0,float("nan"))
        
    if "source_data_staleness_seconds" in df.columns:
        stale=pd.to_numeric(df["source_data_staleness_seconds"],errors="coerce")
        df["_staleness_numeric"]=stale
        st=df.groupby(["run_label","concurrency"])["_staleness_numeric"].mean().reset_index(name="mean_source_data_staleness_seconds")
        summary=summary.merge(st,on=["run_label","concurrency"],how="left")
        
    summary.to_csv(os.path.join(a.output_dir,"analysis_summary.csv"),index=False)
    
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("Pandas analysis saved, but Matplotlib is missing. Run: pip install -r requirements.txt")
        summary.to_markdown(os.path.join(a.output_dir,"analysis_report.md"),index=False)
        return 0
        
    def chart(column,title,ylabel,filename):
        fig,ax=plt.subplots(figsize=(8, 5))
        for label,part in summary.groupby("run_label"):
            part=part.sort_values("concurrency")
            if column in part and part[column].notna().any():
                ax.plot(part["concurrency"],part[column],marker="o",linewidth=2,label=str(label))
        ax.set_title(title, fontsize=12, fontweight="bold")
        ax.set_xlabel("Concurrency (workers)", fontsize=10)
        ax.set_ylabel(ylabel, fontsize=10)
        ax.grid(True,alpha=.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(a.output_dir,filename),dpi=150)
        plt.close(fig)
        
    chart("avg_latency_ms","Average Latency by Concurrency","Latency (ms)","latency_comparison.png")
    chart("p95_latency_ms","P95 Latency by Concurrency","P95 Latency (ms)","p95_latency_comparison.png")
    if "approx_ops_per_second" in summary:
        chart("approx_ops_per_second","Approximate Throughput by Concurrency","Operations/second","throughput_comparison.png")
    chart("observed_hit_rate_percent","Observed Cache Hit Rate by Policy","Hit Rate (%)","hit_rate_comparison.png")
    
    if "server_eviction_count" in summary and summary["server_eviction_count"].notna().any():
        chart("server_eviction_count","Server Eviction Count by Concurrency","Total Evictions","eviction_comparison.png")
        
    report=["# Benchmark Analysis", "", "## Summary", "", summary.to_markdown(index=False), "", "## Interpretation and Observations", "",
      "- Eviction count is reported directly from the server's internal `INFO` metrics counter.",
      "- Cache policies (FIFO, LRU, LFU, RANDOM, CACHEUS) are dynamically switched on the server via `CONFIG SET POLICY`.",
      "- Cache hit rate reflects performance under the configured capacity limit.",
      "- Latency includes client TCP request/reply round-trip time.",
      "- Throughput in this analysis is an approximation from timestamp span; see summary JSON for measured wall-clock throughput.", ""]
    with open(os.path.join(a.output_dir,"analysis_report.md"),"w",encoding="utf-8") as f: f.write("\n".join(report))
    print("Analysis complete:",a.output_dir); print("Summary:",os.path.join(a.output_dir,"analysis_summary.csv")); return 0

if __name__=="__main__": raise SystemExit(main())
