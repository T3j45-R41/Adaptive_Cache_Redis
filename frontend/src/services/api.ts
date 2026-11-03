import { DashboardOverview, BenchmarkComparisonItem, StockQuote, DBMSStats, QueryPerformanceDemo } from '../types';

const API_BASE = '/api';

export const api = {
  async getHealth() {
    const res = await fetch(`${API_BASE}/health`);
    return res.json();
  },

  async getDashboardOverview(): Promise<DashboardOverview> {
    const res = await fetch(`${API_BASE}/dashboard/overview`);
    if (!res.ok) throw new Error('Failed to fetch dashboard overview');
    return res.json();
  },

  async getComparisonMatrix(): Promise<BenchmarkComparisonItem[]> {
    const res = await fetch(`${API_BASE}/benchmark/comparison`);
    if (!res.ok) throw new Error('Failed to fetch comparison matrix');
    return res.json();
  },

  async getAdaptiveWeights(): Promise<any[]> {
    const res = await fetch(`${API_BASE}/analytics/adaptive/weights`);
    if (!res.ok) throw new Error('Failed to fetch adaptive weights');
    return res.json();
  },

  async getStocks(): Promise<StockQuote[]> {
    const res = await fetch(`${API_BASE}/stocks`);
    if (!res.ok) throw new Error('Failed to fetch stocks');
    return res.json();
  },

  async getDBMSStats(): Promise<DBMSStats> {
    const res = await fetch(`${API_BASE}/mysql/summary`);
    if (!res.ok) throw new Error('Failed to fetch DBMS stats');
    return res.json();
  },

  async getQueryPerformance(): Promise<{ engine: string; demonstrations: QueryPerformanceDemo[] }> {
    const res = await fetch(`${API_BASE}/mysql/query-performance`);
    if (!res.ok) throw new Error('Failed to fetch query performance');
    return res.json();
  },

  async setPolicy(policy: string, capacity?: number) {
    const res = await fetch(`${API_BASE}/cache/policy`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ policy, capacity }),
    });
    return res.json();
  },

  async runExperiment(params: {
    experiment_name: string;
    workload_type: string;
    cache_capacity: number;
    total_requests: number;
    seed: number;
  }) {
    const res = await fetch(`${API_BASE}/experiments`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(params),
    });
    if (!res.ok) throw new Error('Failed to run experiment');
    return res.json();
  },
};
