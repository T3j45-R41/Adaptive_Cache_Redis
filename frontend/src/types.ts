export interface DashboardOverview {
  status: string;
  active_policy: string;
  cache_capacity: number;
  current_keys: number;
  utilization_pct: number;
  total_requests: number;
  total_hits: number;
  total_misses: number;
  hit_rate: number;
  miss_rate: number;
  total_evictions: number;
  average_latency: number;
  throughput: number;
  expert_lru_weight: number;
  expert_lfu_weight: number;
  cumulative_regret1: number;
  cumulative_regret2: number;
  active_expert: string;
  recent_benchmarks: BenchmarkComparisonItem[];
  policy_performance: PolicyPerformanceItem[];
}

export interface BenchmarkComparisonItem {
  experiment_id: number;
  experiment_name: string;
  workload_type: string;
  cache_capacity: number;
  policy_name: string;
  is_adaptive: number | boolean;
  hit_rate: number;
  average_latency: number;
  p95_latency: number;
  throughput: number;
  staleness: number;
  execution_time: number;
  created_at: string;
}

export interface PolicyPerformanceItem {
  policy_id: number;
  policy_name: string;
  is_adaptive: number | boolean;
  total_runs: number;
  avg_hit_rate: number;
  min_hit_rate: number;
  max_hit_rate: number;
  avg_latency_ms: number;
  avg_p95_latency_ms: number;
  avg_throughput_rps: number;
  avg_staleness_sec: number;
}

export interface StockQuote {
  symbol: string;
  source_price: number;
  open_price: number;
  high_price: number;
  low_price: number;
  volume: number;
  market_timestamp: string;
  is_cached: boolean;
  cached_price: number | null;
  cached_timestamp: string | null;
  staleness_seconds: number;
}

export interface DBMSStats {
  rdbms_type: string;
  database_name: string;
  tables: Record<string, number>;
  total_records: number;
}

export interface QueryPerformanceDemo {
  query_name: string;
  sql: string;
  execution_time_ms: number;
  row_count: number;
  status: string;
}
