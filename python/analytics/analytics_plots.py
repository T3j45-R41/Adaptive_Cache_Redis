import os
import sys
import matplotlib
matplotlib.use("Agg") # Non-interactive headless backend
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "database"))
from connection import db_manager

def generate_performance_plots(output_dir: str = "results/charts"):
    os.makedirs(output_dir, exist_ok=True)
    print(f"[Analytics] Generating comparative evaluation plots into {output_dir}...")

    # Load comparison data from SQL view
    query = "SELECT * FROM vw_experiment_comparison"
    rows = db_manager.execute_query(query)
    if not rows:
        print("[Analytics] No experiment data found to plot.")
        return

    df = pd.DataFrame(rows)

    # 1. Hit Rate by Policy (Grouped by Workload)
    plt.figure(figsize=(10, 6))
    pivot_hit = df.pivot_table(index="workload_type", columns="policy_name", values="hit_rate", aggfunc="mean")
    pivot_hit.plot(kind="bar", ax=plt.gca(), colormap="viridis", edgecolor="black")
    plt.title("Cache Hit Rate (%) by Eviction Policy & Workload", fontsize=14, fontweight="bold")
    plt.ylabel("Hit Rate (%)", fontsize=12)
    plt.xlabel("Workload Pattern", fontsize=12)
    plt.grid(axis="y", linestyle="--", alpha=0.7)
    plt.legend(title="Policy", bbox_to_anchor=(1.02, 1), loc="upper left")
    plt.tight_layout()
    plot1_path = os.path.join(output_dir, "hit_rate_comparison.png")
    plt.savefig(plot1_path, dpi=200)
    plt.close()

    # 2. Average Latency Comparison (ms)
    plt.figure(figsize=(10, 6))
    pivot_lat = df.pivot_table(index="workload_type", columns="policy_name", values="average_latency", aggfunc="mean")
    pivot_lat.plot(kind="bar", ax=plt.gca(), colormap="magma", edgecolor="black")
    plt.title("Average Request Latency (ms) by Policy", fontsize=14, fontweight="bold")
    plt.ylabel("Average Latency (ms)", fontsize=12)
    plt.xlabel("Workload Pattern", fontsize=12)
    plt.grid(axis="y", linestyle="--", alpha=0.7)
    plt.legend(title="Policy", bbox_to_anchor=(1.02, 1), loc="upper left")
    plt.tight_layout()
    plot2_path = os.path.join(output_dir, "latency_comparison.png")
    plt.savefig(plot2_path, dpi=200)
    plt.close()

    # 3. Throughput (req/sec) Comparison
    plt.figure(figsize=(10, 6))
    pivot_tp = df.pivot_table(index="workload_type", columns="policy_name", values="throughput", aggfunc="mean")
    pivot_tp.plot(kind="bar", ax=plt.gca(), colormap="coolwarm", edgecolor="black")
    plt.title("System Throughput (Requests/sec)", fontsize=14, fontweight="bold")
    plt.ylabel("Throughput (req/s)", fontsize=12)
    plt.xlabel("Workload Pattern", fontsize=12)
    plt.grid(axis="y", linestyle="--", alpha=0.7)
    plt.legend(title="Policy", bbox_to_anchor=(1.02, 1), loc="upper left")
    plt.tight_layout()
    plot3_path = os.path.join(output_dir, "throughput_comparison.png")
    plt.savefig(plot3_path, dpi=200)
    plt.close()

    # 4. Adaptive Expert Weights & Regret Trajectory
    adapt_rows = db_manager.execute_query("SELECT * FROM adaptive_policy_metrics ORDER BY timestamp ASC")
    if adapt_rows:
        adf = pd.DataFrame(adapt_rows)
        plt.figure(figsize=(9, 5))
        for exp_name, group in adf.groupby("expert_name"):
            plt.plot(range(len(group)), group["expert_weight"], marker="o", label=f"Expert: {exp_name} Weight")
        plt.title("Adaptive CACHEUS Expert Weights Evolution", fontsize=14, fontweight="bold")
        plt.xlabel("Weight Update Epoch", fontsize=12)
        plt.ylabel("Normalized Weight $w_i$", fontsize=12)
        plt.ylim(0, 1)
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()
        plot4_path = os.path.join(output_dir, "adaptive_weights_trajectory.png")
        plt.savefig(plot4_path, dpi=200)
        plt.close()

    print(f"[Analytics] Plots successfully saved to {output_dir}")

if __name__ == "__main__":
    generate_performance_plots()
