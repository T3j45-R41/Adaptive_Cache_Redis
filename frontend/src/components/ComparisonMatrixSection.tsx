import React, { useState } from 'react';
import { BenchmarkComparisonItem } from '../types';
import { Download, Filter, BarChart3, TrendingUp, Layers } from 'lucide-react';
import {
  ResponsiveContainer,
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
} from 'recharts';

interface ComparisonMatrixProps {
  data: BenchmarkComparisonItem[];
}

export const ComparisonMatrixSection: React.FC<ComparisonMatrixProps> = ({ data }) => {
  const [selectedWorkload, setSelectedWorkload] = useState<string>('all');
  const [selectedCapacity, setSelectedCapacity] = useState<string>('all');
  const [sortBy, setSortBy] = useState<string>('hit_rate');
  const [sortDesc, setSortDesc] = useState<boolean>(true);

  // Extract unique filters
  const workloads = Array.from(new Set(data.map((d) => d.workload_type)));
  const capacities = Array.from(new Set(data.map((d) => d.cache_capacity)));

  // Filtered dataset
  const filteredData = data.filter((item) => {
    if (selectedWorkload !== 'all' && item.workload_type !== selectedWorkload) return false;
    if (selectedCapacity !== 'all' && item.cache_capacity.toString() !== selectedCapacity) return false;
    return true;
  });

  // Sorted dataset
  const sortedData = [...filteredData].sort((a, b) => {
    let valA = (a as any)[sortBy];
    let valB = (b as any)[sortBy];
    if (typeof valA === 'string') {
      return sortDesc ? valB.localeCompare(valA) : valA.localeCompare(valB);
    }
    return sortDesc ? valB - valA : valA - valB;
  });

  // Prepare chart data: average by policy for currently selected filters
  const policyAggregates = ['LRU', 'LFU', 'FIFO', 'RANDOM', 'ADAPTIVE'].map((pol) => {
    const matches = filteredData.filter((d) => d.policy_name === pol);
    if (!matches.length) {
      return { policy: pol, hit_rate: 0, latency: 0, throughput: 0, staleness: 0 };
    }
    const avgHit = matches.reduce((acc, m) => acc + m.hit_rate, 0) / matches.length;
    const avgLat = matches.reduce((acc, m) => acc + m.average_latency, 0) / matches.length;
    const avgTp = matches.reduce((acc, m) => acc + m.throughput, 0) / matches.length;
    const avgStale = matches.reduce((acc, m) => acc + m.staleness, 0) / matches.length;
    return {
      policy: pol,
      hit_rate: Number(avgHit.toFixed(2)),
      latency: Number(avgLat.toFixed(3)),
      throughput: Number(avgTp.toFixed(0)),
      staleness: Number(avgStale.toFixed(2)),
    };
  });

  const exportCSV = () => {
    if (!sortedData.length) return;
    const headers = [
      'experiment_id',
      'experiment_name',
      'workload_type',
      'cache_capacity',
      'policy_name',
      'hit_rate',
      'average_latency',
      'p95_latency',
      'throughput',
      'staleness',
      'execution_time',
      'created_at',
    ];
    const rows = sortedData.map((d) =>
      headers.map((h) => JSON.stringify((d as any)[h] ?? '')).join(',')
    );
    const csvContent = 'data:text/csv;charset=utf-8,' + [headers.join(','), ...rows].join('\n');
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement('a');
    link.setAttribute('href', encodedUri);
    link.setAttribute('download', `adaptive_cache_benchmark_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  return (
    <div className="space-y-6">
      {/* Controls Bar: Filters & CSV Export */}
      <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs flex flex-wrap items-center justify-between gap-4">
        <div className="flex flex-wrap items-center gap-3 text-xs">
          <div className="flex items-center gap-1.5 text-ctp-subtext0 font-medium">
            <Filter className="w-3.5 h-3.5 text-ctp-blue" />
            <span>Filters:</span>
          </div>

          {/* Workload Filter */}
          <select
            value={selectedWorkload}
            onChange={(e) => setSelectedWorkload(e.target.value)}
            className="bg-ctp-mantle border border-ctp-surface1 rounded-lg px-2.5 py-1.5 text-ctp-text font-mono text-xs focus:ring-1 focus:ring-ctp-blue focus:outline-none"
          >
            <option value="all">All Workloads</option>
            {workloads.map((w) => (
              <option key={w} value={w}>
                {w}
              </option>
            ))}
          </select>

          {/* Capacity Filter */}
          <select
            value={selectedCapacity}
            onChange={(e) => setSelectedCapacity(e.target.value)}
            className="bg-ctp-mantle border border-ctp-surface1 rounded-lg px-2.5 py-1.5 text-ctp-text font-mono text-xs focus:ring-1 focus:ring-ctp-blue focus:outline-none"
          >
            <option value="all">All Capacities</option>
            {capacities.map((c) => (
              <option key={c} value={c.toString()}>
                Capacity: {c}
              </option>
            ))}
          </select>

          <span className="text-ctp-surface2">|</span>
          <span className="text-ctp-subtext0 font-mono">
            Showing <strong className="text-ctp-text">{sortedData.length}</strong> benchmark runs
          </span>
        </div>

        <button
          onClick={exportCSV}
          disabled={!sortedData.length}
          className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-ctp-blue hover:bg-ctp-sapphire text-white text-xs font-medium transition shadow-xs disabled:opacity-50"
        >
          <Download className="w-3.5 h-3.5" />
          <span>Export Matrix (CSV)</span>
        </button>
      </div>

      {/* Comparison Charts Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Hit Rate Bar Chart */}
        <div className="p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="text-sm font-semibold text-ctp-text">Comparative Hit Rate (%)</h3>
              <p className="text-xs text-ctp-subtext0">Higher is better</p>
            </div>
            <BarChart3 className="w-4 h-4 text-ctp-green" />
          </div>
          <div className="h-64">
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={policyAggregates}>
                <CartesianGrid strokeDasharray="3 3" stroke="#e6e9ef" />
                <XAxis dataKey="policy" stroke="#7c7f93" tick={{ fontSize: 11 }} />
                <YAxis stroke="#7c7f93" domain={[0, 100]} tick={{ fontSize: 11 }} unit="%" />
                <Tooltip
                  contentStyle={{ backgroundColor: '#ffffff', borderColor: '#ccd0da', borderRadius: '8px', color: '#4c4f69' }}
                  itemStyle={{ color: '#4c4f69', fontSize: '12px' }}
                />
                <Bar dataKey="hit_rate" fill="#40a02b" radius={[4, 4, 0, 0]} name="Hit Rate (%)" />
              </BarChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Latency Comparison Chart */}
        <div className="p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="text-sm font-semibold text-ctp-text">Average Request Latency (ms)</h3>
              <p className="text-xs text-ctp-subtext0">Lower is better</p>
            </div>
            <TrendingUp className="w-4 h-4 text-ctp-peach" />
          </div>
          <div className="h-64">
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={policyAggregates}>
                <CartesianGrid strokeDasharray="3 3" stroke="#e6e9ef" />
                <XAxis dataKey="policy" stroke="#7c7f93" tick={{ fontSize: 11 }} />
                <YAxis stroke="#7c7f93" tick={{ fontSize: 11 }} unit="ms" />
                <Tooltip
                  contentStyle={{ backgroundColor: '#ffffff', borderColor: '#ccd0da', borderRadius: '8px', color: '#4c4f69' }}
                  itemStyle={{ color: '#4c4f69', fontSize: '12px' }}
                />
                <Bar dataKey="latency" fill="#fe640b" radius={[4, 4, 0, 0]} name="Latency (ms)" />
              </BarChart>
            </ResponsiveContainer>
          </div>
        </div>
      </div>

      {/* Comprehensive Filterable Table */}
      <div className="p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
        <div className="mb-4">
          <h3 className="text-sm font-semibold text-ctp-text">Complete Performance Matrix</h3>
          <p className="text-xs text-ctp-subtext0">Sort by clicking column headers</p>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs">
            <thead>
              <tr className="border-b border-ctp-surface0 text-ctp-subtext0 font-mono">
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('experiment_id');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Exp #
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('policy_name');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Policy
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('workload_type');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Workload
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('cache_capacity');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Capacity
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('hit_rate');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Hit Rate (%)
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('average_latency');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Avg Latency
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('p95_latency');
                    setSortDesc(!sortDesc);
                  }}
                >
                  P95 Latency
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('throughput');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Throughput
                </th>
                <th
                  className="pb-2.5 font-medium cursor-pointer hover:text-ctp-text transition"
                  onClick={() => {
                    setSortBy('staleness');
                    setSortDesc(!sortDesc);
                  }}
                >
                  Staleness
                </th>
              </tr>
            </thead>
            <tbody className="divide-y divide-ctp-surface0/60 font-mono">
              {sortedData.map((row, idx) => (
                <tr key={`${row.experiment_id}-${row.policy_name}-${idx}`} className="hover:bg-ctp-mantle/70 transition">
                  <td className="py-2.5 text-ctp-subtext0">#{row.experiment_id}</td>
                  <td className="py-2.5 font-semibold text-ctp-text flex items-center gap-1.5">
                    <span>{row.policy_name}</span>
                    {row.is_adaptive ? (
                      <span className="text-[10px] px-1 py-0.2 rounded bg-ctp-mauve/15 text-ctp-mauve border border-ctp-mauve/30">
                        Adaptive
                      </span>
                    ) : null}
                  </td>
                  <td className="py-2.5 text-ctp-subtext1">{row.workload_type}</td>
                  <td className="py-2.5 text-ctp-subtext1">{row.cache_capacity}</td>
                  <td className="py-2.5 font-bold text-ctp-green">{row.hit_rate}%</td>
                  <td className="py-2.5 text-ctp-subtext1">{row.average_latency} ms</td>
                  <td className="py-2.5 text-ctp-subtext1">{row.p95_latency} ms</td>
                  <td className="py-2.5 text-ctp-mauve font-semibold">{row.throughput} rps</td>
                  <td className="py-2.5 text-ctp-peach font-semibold">{row.staleness} s</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
};
