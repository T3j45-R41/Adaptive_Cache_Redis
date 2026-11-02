import React from 'react';
import { DashboardOverview } from '../types';
import { Cpu, Brain, GitCommit, Sliders, Scale, Info } from 'lucide-react';
import { ResponsiveContainer, LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend } from 'recharts';

interface AdaptiveMonitoringProps {
  overview: DashboardOverview;
  weightsHistory: any[];
}

export const AdaptiveMonitoringSection: React.FC<AdaptiveMonitoringProps> = ({
  overview,
  weightsHistory,
}) => {
  const w1 = overview.expert_lru_weight ?? 0.5;
  const w2 = overview.expert_lfu_weight ?? 0.5;
  const r1 = overview.cumulative_regret1 ?? 0.0;
  const r2 = overview.cumulative_regret2 ?? 0.0;

  // Chart data formatting
  const chartData = weightsHistory.map((item, idx) => ({
    step: idx + 1,
    weight: item.expert_weight,
    expert: item.expert_name,
    regret: item.cumulative_regret,
  }));

  return (
    <div className="space-y-6">
      {/* Top Banner: Real-time Expert State */}
      <div className="p-6 rounded-2xl bg-gradient-to-r from-indigo-950/60 via-slate-900/80 to-slate-900 border border-indigo-900/40 relative overflow-hidden">
        <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
          <div>
            <div className="flex items-center gap-2 mb-1">
              <Brain className="w-5 h-5 text-indigo-400" />
              <h2 className="text-base font-bold text-white tracking-tight">Adaptive CACHEUS Eviction Engine</h2>
              <span className="text-xs px-2 py-0.5 rounded-full font-mono bg-indigo-500/20 text-indigo-300 border border-indigo-500/30">
                Online Learning Active
              </span>
            </div>
            <p className="text-xs text-slate-400 max-w-2xl">
              Dynamically blends Recency (LRU) and Frequency (LFU) shadow policy experts via Multiplicative
              Weight Updates (Hedge Algorithm) to minimize cumulative regret under changing stock quote patterns.
            </p>
          </div>

          <div className="p-3 rounded-xl bg-slate-950/70 border border-slate-800 text-right font-mono">
            <span className="text-[11px] text-slate-400 block">Current Active Expert</span>
            <span className="text-lg font-bold text-indigo-400">{overview.active_expert || 'LRU'}</span>
          </div>
        </div>
      </div>

      {/* Expert Weight Breakdown Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* Expert 1: LRU (Recency) */}
        <div className="p-5 rounded-2xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
          <div>
            <div className="flex items-center justify-between mb-2">
              <span className="text-xs font-semibold text-slate-300">Expert 1: Recency (LRU)</span>
              <span className="text-xs font-mono font-bold text-sky-400">{(w1 * 100).toFixed(1)}% Weight</span>
            </div>
            <div className="w-full bg-slate-800 rounded-full h-2 mb-3 overflow-hidden">
              <div
                className="bg-sky-500 h-2 rounded-full transition-all duration-500"
                style={{ width: `${w1 * 100}%` }}
              />
            </div>
            <p className="text-xs text-slate-400">
              Favors recently queried stock quotes. Excels during normal market cycles and sequential ticker scans.
            </p>
          </div>
          <div className="mt-4 pt-3 border-t border-slate-800/80 flex justify-between text-xs font-mono text-slate-400">
            <span>Cumulative Regret (R₁):</span>
            <span className="text-white font-medium">{r1.toFixed(2)}</span>
          </div>
        </div>

        {/* Expert 2: LFU (Frequency) */}
        <div className="p-5 rounded-2xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
          <div>
            <div className="flex items-center justify-between mb-2">
              <span className="text-xs font-semibold text-slate-300">Expert 2: Frequency (LFU)</span>
              <span className="text-xs font-mono font-bold text-indigo-400">{(w2 * 100).toFixed(1)}% Weight</span>
            </div>
            <div className="w-full bg-slate-800 rounded-full h-2 mb-3 overflow-hidden">
              <div
                className="bg-indigo-500 h-2 rounded-full transition-all duration-500"
                style={{ width: `${w2 * 100}%` }}
              />
            </div>
            <p className="text-xs text-slate-400">
              Favors hot keys with repeat access history. Excels during Zipfian distributions and concentrated trading.
            </p>
          </div>
          <div className="mt-4 pt-3 border-t border-slate-800/80 flex justify-between text-xs font-mono text-slate-400">
            <span>Cumulative Regret (R₂):</span>
            <span className="text-white font-medium">{r2.toFixed(2)}</span>
          </div>
        </div>
      </div>

      {/* Regret Minimization Mathematics */}
      <div className="p-5 rounded-2xl bg-slate-900/60 border border-slate-800">
        <div className="flex items-center gap-2 mb-3 text-slate-200">
          <Info className="w-4 h-4 text-sky-400" />
          <h3 className="text-sm font-semibold">Mathematical Formulation (Hedge / LeCaR Adaptation)</h3>
        </div>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 text-xs font-mono text-slate-300">
          <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800/80">
            <span className="text-slate-400 text-[11px] block mb-1">1. Expert Probability Distribution</span>
            <code className="text-sky-300">P(E₁) = w₁ / (w₁ + w₂)</code>
            <p className="text-[11px] text-slate-400 mt-1 font-sans">
              Eviction victim is selected probabilistically based on current weights.
            </p>
          </div>

          <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800/80">
            <span className="text-slate-400 text-[11px] block mb-1">2. Shadow Feedback & Regret</span>
            <code className="text-amber-300">r₂ = 1.0 if key ∈ Shadow(E₁)</code>
            <p className="text-[11px] text-slate-400 mt-1 font-sans">
              If a missed key was in E₁'s shadow history, E₂ incurs regret r₂ = 1.0.
            </p>
          </div>

          <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800/80">
            <span className="text-slate-400 text-[11px] block mb-1">3. Multiplicative Weight Update</span>
            <code className="text-emerald-300">wᵢ ← wᵢ · exp(-η · rᵢ)</code>
            <p className="text-[11px] text-slate-400 mt-1 font-sans">
              Adaptive learning rate η = 0.05 smoothly adjusts expert confidence.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};
