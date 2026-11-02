import React from 'react';
import { DashboardOverview } from '../types';
import { ShieldCheck, HardDrive, Target, Zap, Clock, Trash2, ArrowUpRight } from 'lucide-react';
import { ResponsiveContainer, PieChart, Pie, Cell, Tooltip } from 'recharts';

interface OverviewSectionProps {
  data: DashboardOverview;
  onNavigateTab: (tab: string) => void;
}

export const OverviewSection: React.FC<OverviewSectionProps> = ({ data, onNavigateTab }) => {
  const pieData = [
    { name: 'Cache Hits', value: data.total_hits, color: '#10b981' },
    { name: 'Cache Misses', value: data.total_misses, color: '#ef4444' },
  ];

  return (
    <div className="space-y-6">
      {/* Metric Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-6 gap-4">
        {/* Active Policy */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm relative overflow-hidden">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Active Policy</span>
            <ShieldCheck className="w-4 h-4 text-sky-400" />
          </div>
          <div className="text-xl font-bold font-mono text-white">{data.active_policy}</div>
          <p className="text-[11px] text-slate-400 mt-1">
            {data.active_policy === 'ADAPTIVE' ? `Expert: ${data.active_expert}` : 'Static Algorithm'}
          </p>
        </div>

        {/* Cache Capacity & Utilization */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Capacity Utilization</span>
            <HardDrive className="w-4 h-4 text-indigo-400" />
          </div>
          <div className="text-xl font-bold font-mono text-white">
            {data.current_keys} <span className="text-xs font-normal text-slate-400">/ {data.cache_capacity}</span>
          </div>
          <div className="w-full bg-slate-800 rounded-full h-1.5 mt-2 overflow-hidden">
            <div 
              className="bg-indigo-500 h-1.5 rounded-full transition-all duration-500" 
              style={{ width: `${Math.min(100, data.utilization_pct)}%` }}
            />
          </div>
        </div>

        {/* Hit Rate */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Cache Hit Rate</span>
            <Target className="w-4 h-4 text-emerald-400" />
          </div>
          <div className="text-xl font-bold font-mono text-emerald-400">
            {data.hit_rate.toFixed(1)}%
          </div>
          <p className="text-[11px] text-slate-400 mt-1">
            Hits: {data.total_hits} | Miss: {data.total_misses}
          </p>
        </div>

        {/* Latency */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Avg Latency</span>
            <Clock className="w-4 h-4 text-amber-400" />
          </div>
          <div className="text-xl font-bold font-mono text-white">
            {data.average_latency > 0 ? `${data.average_latency.toFixed(3)} ms` : '< 0.05 ms'}
          </div>
          <p className="text-[11px] text-slate-400 mt-1">In-memory sub-millisecond</p>
        </div>

        {/* Throughput */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Throughput</span>
            <Zap className="w-4 h-4 text-purple-400" />
          </div>
          <div className="text-xl font-bold font-mono text-white">
            {data.throughput > 0 ? `${data.throughput.toFixed(0)} rps` : 'Idle'}
          </div>
          <p className="text-[11px] text-slate-400 mt-1">Bounded worker pool</p>
        </div>

        {/* Total Evictions */}
        <div className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm">
          <div className="flex items-center justify-between text-slate-400 mb-2">
            <span className="text-xs font-medium">Evictions</span>
            <Trash2 className="w-4 h-4 text-rose-400" />
          </div>
          <div className="text-xl font-bold font-mono text-rose-400">
            {data.total_evictions}
          </div>
          <p className="text-[11px] text-slate-400 mt-1">Enforced at capacity</p>
        </div>
      </div>

      {/* Main Grid: Hits/Misses Breakdown + Performance Aggregation */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Hit/Miss Donut */}
        <div className="p-5 rounded-2xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
          <div>
            <h3 className="text-sm font-semibold text-slate-200">Cache Traffic Distribution</h3>
            <p className="text-xs text-slate-400 mt-0.5">Ratio of hits to misses across requests</p>
          </div>
          <div className="h-52 w-full flex items-center justify-center my-2">
            {data.total_requests > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <PieChart>
                  <Pie
                    data={pieData}
                    cx="50%"
                    cy="50%"
                    innerRadius={55}
                    outerRadius={80}
                    paddingAngle={4}
                    dataKey="value"
                  >
                    {pieData.map((entry, index) => (
                      <Cell key={`cell-${index}`} fill={entry.color} />
                    ))}
                  </Pie>
                  <Tooltip 
                    contentStyle={{ backgroundColor: '#0f172a', borderColor: '#334155', borderRadius: '8px' }}
                    itemStyle={{ color: '#f8fafc', fontSize: '12px' }}
                  />
                </PieChart>
              </ResponsiveContainer>
            ) : (
              <div className="text-xs text-slate-500 font-mono">No requests recorded yet</div>
            )}
          </div>
          <div className="flex justify-around border-t border-slate-800/80 pt-3">
            <div className="flex items-center gap-2">
              <span className="w-3 h-3 rounded-full bg-emerald-500" />
              <span className="text-xs text-slate-300">Hits ({data.hit_rate.toFixed(1)}%)</span>
            </div>
            <div className="flex items-center gap-2">
              <span className="w-3 h-3 rounded-full bg-rose-500" />
              <span className="text-xs text-slate-300">Misses ({data.miss_rate.toFixed(1)}%)</span>
            </div>
          </div>
        </div>

        {/* Policy Performance Aggregated from MySQL */}
        <div className="lg:col-span-2 p-5 rounded-2xl bg-slate-900/60 border border-slate-800 flex flex-col">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="text-sm font-semibold text-slate-200">Historical Policy Performance (MySQL View)</h3>
              <p className="text-xs text-slate-400 mt-0.5">Aggregated metrics from <code className="text-sky-400">vw_policy_performance</code></p>
            </div>
            <button
              onClick={() => onNavigateTab('comparison')}
              className="text-xs text-sky-400 hover:text-sky-300 flex items-center gap-1 font-medium"
            >
              <span>View Full Matrix</span>
              <ArrowUpRight className="w-3.5 h-3.5" />
            </button>
          </div>

          <div className="overflow-x-auto flex-1">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="border-b border-slate-800 text-slate-400 font-mono">
                  <th className="pb-2 font-medium">Policy</th>
                  <th className="pb-2 font-medium">Runs</th>
                  <th className="pb-2 font-medium">Avg Hit Rate</th>
                  <th className="pb-2 font-medium">Avg Latency</th>
                  <th className="pb-2 font-medium">Throughput</th>
                  <th className="pb-2 font-medium">Avg Staleness</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800/60 font-mono">
                {data.policy_performance && data.policy_performance.length > 0 ? (
                  data.policy_performance.map((item) => (
                    <tr key={item.policy_name} className="hover:bg-slate-800/30 transition">
                      <td className="py-2.5 font-bold text-white flex items-center gap-2">
                        <span>{item.policy_name}</span>
                        {item.is_adaptive ? (
                          <span className="text-[10px] px-1.5 py-0.2 rounded bg-indigo-500/20 text-indigo-400 border border-indigo-500/30">
                            Adaptive
                          </span>
                        ) : null}
                      </td>
                      <td className="py-2.5 text-slate-300">{item.total_runs}</td>
                      <td className="py-2.5 font-semibold text-emerald-400">{item.avg_hit_rate}%</td>
                      <td className="py-2.5 text-slate-300">{item.avg_latency_ms} ms</td>
                      <td className="py-2.5 text-slate-300">{item.avg_throughput_rps} rps</td>
                      <td className="py-2.5 text-amber-400">{item.avg_staleness_sec} s</td>
                    </tr>
                  ))
                ) : (
                  <tr>
                    <td colSpan={6} className="py-6 text-center text-slate-500">
                      Run benchmarks from the Experiment Runner tab to populate metrics.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
  );
};
