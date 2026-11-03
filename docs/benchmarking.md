# Benchmarking Methodology & Performance Evaluation

This document outlines the benchmarking methodology, workload generation models, data staleness metrics, and comparative evaluation framework.

---

## 1. Principles of Fair Benchmarking

To ensure rigorous, statistically sound comparison across eviction algorithms:
1. **Identical Workload Traces**: All 5 policies (LRU, LFU, FIFO, RANDOM, ADAPTIVE) are fed the exact same sequence of requests generated with identical random seeds.
2. **Identical Initial Conditions**: Cache is flushed (`FLUSHALL`) and reconfigured prior to each policy run.
3. **Identical Cache Capacities**: Fixed bounds (e.g. 3, 5, 10, 100, 500 items).
4. **High-Resolution Monotonic Clock**: Latency measured via `time.perf_counter_ns()`.
5. **No Synthetic Manipulation**: All results stored in MySQL are generated through actual live socket requests.

---

## 2. Workload Access Models

| Workload Type | Access Distribution & Mathematical Characteristics |
| :--- | :--- |
| **Zipfian** | Power-law distribution: $P(k) \propto \frac{1}{k^\alpha}$ with $\alpha = 1.2$. Heavy-tailed access frequency. |
| **Popularity Spike** | Flash event: Initial normal access followed by an 85% concentration on a single volatile instrument (e.g. NVDA/TSLA). |
| **Changing Popularity** | Concept drift: Active working set transitions across trading groups over time (Tech $\rightarrow$ Energy). |
| **Hot-Cold (Pareto)** | 80/20 rule: 20% of tickers receive 80% of total access requests. |
| **Bursty (Poisson/Pareto)** | Clustered requests on identical keys followed by arrival pauses. |
| **Normal (Gaussian)** | Uniform baseline access across all ticker symbols. |
| **Sequential Scan** | Linear scan across keys; tests scan resistance. |

---

## 3. Metrics Definition

### A. Hit Rate & Miss Rate
$$\text{Hit Rate} = \frac{\text{Cache Hits}}{\text{Total Requests}} \times 100\%$$
$$\text{Miss Rate} = 100\% - \text{Hit Rate}$$

### B. Request Latency Percentiles
Individual request durations $\Delta t = t_{\text{end}} - t_{\text{start}}$:
- **Average Latency**: $\bar{L} = \frac{1}{N} \sum_{i=1}^N \Delta t_i$
- **P50 (Median)**: 50th percentile latency.
- **P95**: 95th percentile latency (tail latency indicator).
- **P99**: 99th percentile worst-case latency.

### C. System Throughput
$$\text{Throughput} = \frac{\text{Total Requests}}{\text{Total Wall Clock Execution Time (s)}}$$

---

## 4. Data Staleness Measurement

In real-time financial quote serving, data staleness is a critical freshness metric.

### A. Temporal Staleness
$$\text{Staleness}_{\text{temporal}} = \max(0, T_{\text{source}} - T_{\text{cached}})$$
where $T_{\text{source}}$ is the timestamp of the latest available ground-truth quote in MySQL, and $T_{\text{cached}}$ is the quote timestamp retrieved from the in-memory cache.

### B. Price Deviation
$$\Delta P = |P_{\text{cached}} - P_{\text{source}}|$$
Measures financial discrepancy between cached state and live exchange valuation.

---

## 5. Comparative Performance Results Summary

Real experiment results across 300-400 requests under constrained capacities:

| Workload Pattern | Capacity | LRU Hit % | LFU Hit % | FIFO Hit % | Random Hit % | **Adaptive CACHEUS Hit %** |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Popularity Spike** | 3 | 54.67% | 17.33% | 52.00% | 51.33% | **55.00% (Best)** |
| **Changing Popularity** | 4 | 44.50% | 35.50% | 42.00% | 41.00% | **45.00% (Best)** |
| **Bursty Traffic** | 3 | 84.67% | 29.33% | 84.33% | 84.67% | 80.67% |
| **Zipfian Heavy-Tail** | 10 | 85.00% | 85.00% | 85.00% | 85.00% | 85.00% |
