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
    { name: 'Cache Hits', value: data.total_hits, color: '#40a02b' },
    { name: 'Cache Misses', value: data.total_misses, color: '#d20f39' },
  ];

  return (
    <div className="space-y-6">
      {/* Metric Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-6 gap-4">
        {/* Active Policy */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs relative overflow-hidden">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Active Policy</span>
            <ShieldCheck className="w-4 h-4 text-ctp-blue" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-text">{data.active_policy}</div>
          <p className="text-[11px] text-ctp-subtext0 mt-1">
            {data.active_policy === 'ADAPTIVE' ? `Expert: ${data.active_expert}` : 'Static Algorithm'}
          </p>
        </div>

        {/* Cache Capacity & Utilization */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Capacity Utilization</span>
            <HardDrive className="w-4 h-4 text-ctp-mauve" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-text">
            {data.current_keys} <span className="text-xs font-normal text-ctp-subtext0">/ {data.cache_capacity}</span>
          </div>
          <div className="w-full bg-ctp-surface0 rounded-full h-1.5 mt-2 overflow-hidden">
            <div 
              className="bg-ctp-mauve h-1.5 rounded-full transition-all duration-500" 
              style={{ width: `${Math.min(100, data.utilization_pct)}%` }}
            />
          </div>
        </div>

        {/* Hit Rate */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Cache Hit Rate</span>
            <Target className="w-4 h-4 text-ctp-green" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-green">
            {data.hit_rate.toFixed(1)}%
          </div>
          <p className="text-[11px] text-ctp-subtext0 mt-1">
            Hits: {data.total_hits} | Miss: {data.total_misses}
          </p>
        </div>

        {/* Latency */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Avg Latency</span>
            <Clock className="w-4 h-4 text-ctp-peach" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-text">
            {data.average_latency > 0 ? `${data.average_latency.toFixed(3)} ms` : '< 0.05 ms'}
          </div>
          <p className="text-[11px] text-ctp-subtext0 mt-1">In-memory sub-millisecond</p>
        </div>

        {/* Throughput */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Throughput</span>
            <Zap className="w-4 h-4 text-ctp-mauve" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-text">
            {data.throughput > 0 ? `${data.throughput.toFixed(0)} rps` : 'Idle'}
          </div>
          <p className="text-[11px] text-ctp-subtext0 mt-1">Bounded worker pool</p>
        </div>

        {/* Total Evictions */}
        <div className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs">
          <div className="flex items-center justify-between text-ctp-subtext0 mb-2">
            <span className="text-xs font-medium">Evictions</span>
            <Trash2 className="w-4 h-4 text-ctp-red" />
          </div>
          <div className="text-xl font-bold font-mono text-ctp-red">
            {data.total_evictions}
          </div>
          <p className="text-[11px] text-ctp-subtext0 mt-1">Enforced at capacity</p>
        </div>
      </div>

      {/* Main Grid: Hits/Misses Breakdown + Performance Aggregation */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Hit/Miss Donut */}
        <div className="p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs flex flex-col justify-between">
          <div>
            <h3 className="text-sm font-semibold text-ctp-text">Cache Traffic Distribution</h3>
            <p className="text-xs text-ctp-subtext0 mt-0.5">Ratio of hits to misses across requests</p>
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
                    contentStyle={{ backgroundColor: '#ffffff', borderColor: '#ccd0da', borderRadius: '8px', color: '#4c4f69' }}
                    itemStyle={{ color: '#4c4f69', fontSize: '12px' }}
                  />
                </PieChart>
              </ResponsiveContainer>
            ) : (
              <div className="text-xs text-ctp-overlay1 font-mono">No requests recorded yet</div>
            )}
          </div>
          <div className="flex justify-around border-t border-ctp-surface0 pt-3">
            <div className="flex items-center gap-2">
              <span className="w-3 h-3 rounded-full bg-ctp-green" />
              <span className="text-xs text-ctp-text font-medium">Hits ({data.hit_rate.toFixed(1)}%)</span>
            </div>
            <div className="flex items-center gap-2">
              <span className="w-3 h-3 rounded-full bg-ctp-red" />
              <span className="text-xs text-ctp-text font-medium">Misses ({data.miss_rate.toFixed(1)}%)</span>
            </div>
          </div>
        </div>

        {/* Policy Performance Aggregated from MySQL */}
        <div className="lg:col-span-2 p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs flex flex-col">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="text-sm font-semibold text-ctp-text">Historical Policy Performance (MySQL View)</h3>
              <p className="text-xs text-ctp-subtext0 mt-0.5">Aggregated metrics from <code className="text-ctp-blue font-mono">vw_policy_performance</code></p>
            </div>
            <button
              onClick={() => onNavigateTab('comparison')}
              className="text-xs text-ctp-blue hover:text-ctp-sapphire flex items-center gap-1 font-medium transition"
            >
              <span>View Full Matrix</span>
              <ArrowUpRight className="w-3.5 h-3.5" />
            </button>
          </div>

          <div className="overflow-x-auto flex-1">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="border-b border-ctp-surface0 text-ctp-subtext0 font-mono">
                  <th className="pb-2 font-medium">Policy</th>
                  <th className="pb-2 font-medium">Total Runs</th>
                  <th className="pb-2 font-medium">Avg Hit Rate</th>
                  <th className="pb-2 font-medium">Min Hit Rate</th>
                  <th className="pb-2 font-medium">Max Hit Rate</th>
                  <th className="pb-2 font-medium">Avg Latency</th>
                  <th className="pb-2 font-medium">Avg Throughput</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-ctp-surface0/70 font-mono">
                {data.policy_performance && data.policy_performance.length > 0 ? (
                  data.policy_performance.map((pol) => (
                    <tr key={pol.policy_name} className="hover:bg-ctp-mantle/70 transition">
                      <td className="py-2.5 font-bold text-ctp-text">{pol.policy_name}</td>
                      <td className="py-2.5 text-ctp-subtext1">{pol.total_runs}</td>
                      <td className="py-2.5 font-bold text-ctp-green">{Number(pol.avg_hit_rate).toFixed(1)}%</td>
                      <td className="py-2.5 text-ctp-subtext1">{Number(pol.min_hit_rate).toFixed(1)}%</td>
                      <td className="py-2.5 text-ctp-subtext1">{Number(pol.max_hit_rate).toFixed(1)}%</td>
                      <td className="py-2.5 text-ctp-subtext1">{Number(pol.avg_latency_ms).toFixed(3)} ms</td>
                      <td className="py-2.5 text-ctp-mauve font-semibold">{Number(pol.avg_throughput_rps).toFixed(0)} rps</td>
                    </tr>
                  ))
                ) : (
                  <tr>
                    <td colSpan={7} className="py-6 text-center text-ctp-overlay1">
                      No policy runs aggregated yet. Run benchmark experiments to populate.
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
