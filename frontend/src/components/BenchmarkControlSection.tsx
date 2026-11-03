import React, { useState } from 'react';
import { Play, CheckCircle, AlertCircle, RefreshCw, Cpu, Layers, Sparkles } from 'lucide-react';
import { api } from '../services/api';

interface BenchmarkControlProps {
  onExperimentComplete: () => void;
}

export const BenchmarkControlSection: React.FC<BenchmarkControlProps> = ({ onExperimentComplete }) => {
  const [expName, setExpName] = useState('Stock Quote Evaluation');
  const [workload, setWorkload] = useState('zipfian');
  const [capacity, setCapacity] = useState<number>(500);
  const [requests, setRequests] = useState<number>(500);
  const [seed, setSeed] = useState<number>(42);
  const [isRunning, setIsRunning] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<any | null>(null);

  const handleRun = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsRunning(true);
    setError(null);
    setLastResult(null);

    try {
      const res = await api.runExperiment({
        experiment_name: expName,
        workload_type: workload,
        cache_capacity: capacity,
        total_requests: requests,
        seed: seed,
      });
      setLastResult(res);
      onExperimentComplete();
    } catch (err: any) {
      setError(err.message || 'Failed to execute benchmark suite');
    } finally {
      setIsRunning(false);
    }
  };

  return (
    <div className="space-y-6">
      {/* Configuration Form Card */}
      <div className="p-6 rounded-2xl bg-slate-900/60 border border-slate-800">
        <div className="flex items-center gap-2 mb-4">
          <Cpu className="w-5 h-5 text-sky-400" />
          <h2 className="text-base font-bold text-white tracking-tight">Experiment Control Panel</h2>
        </div>
        <p className="text-xs text-slate-400 mb-6">
          Executes identical synthetic/historical workload traces across all 5 eviction policies (LRU, LFU, FIFO, RANDOM, ADAPTIVE)
          under identical capacity limits to ensure statistically rigorous comparison.
        </p>

        <form onSubmit={handleRun} className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
          {/* Experiment Name */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1.5">Experiment Title</label>
            <input
              type="text"
              value={expName}
              onChange={(e) => setExpName(e.target.value)}
              required
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:ring-1 focus:ring-sky-500 focus:outline-none"
            />
          </div>

          {/* Workload Pattern */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1.5">Workload Pattern</label>
            <select
              value={workload}
              onChange={(e) => setWorkload(e.target.value)}
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:ring-1 focus:ring-sky-500 focus:outline-none"
            >
              <option value="zipfian">Zipfian Distribution (Power-law α = 1.2)</option>
              <option value="normal">Normal / Uniform Trading</option>
              <option value="popularity_spike">Sudden Popularity Spike (Flash Event)</option>
              <option value="changing_popularity">Changing Popularity (Concept Drift)</option>
              <option value="hot_cold">Hot-Cold Key Access (80/20 Pareto)</option>
              <option value="bursty">Bursty Access Patterns</option>
              <option value="sequential">Sequential Scan</option>
            </select>
          </div>

          {/* Cache Capacity */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1.5">Cache Capacity (Items)</label>
            <select
              value={capacity}
              onChange={(e) => setCapacity(Number(e.target.value))}
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:ring-1 focus:ring-sky-500 focus:outline-none"
            >
              <option value={3}>3 (Severe Capacity Constraint)</option>
              <option value={5}>5 (Constrained Memory)</option>
              <option value={10}>10 (Standard Cache Size)</option>
              <option value={100}>100 (Medium Cache)</option>
              <option value={500}>500 (High Capacity)</option>
              <option value={1000}>1000 (Full In-Memory)</option>
            </select>
          </div>

          {/* Request Count */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1.5">Total Requests</label>
            <input
              type="number"
              min={100}
              max={10000}
              step={100}
              value={requests}
              onChange={(e) => setRequests(Number(e.target.value))}
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:ring-1 focus:ring-sky-500 focus:outline-none"
            />
          </div>

          {/* Random Seed */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1.5">Deterministic Seed</label>
            <input
              type="number"
              value={seed}
              onChange={(e) => setSeed(Number(e.target.value))}
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:ring-1 focus:ring-sky-500 focus:outline-none"
            />
          </div>

          {/* Submit Button */}
          <div className="flex items-end">
            <button
              type="submit"
              disabled={isRunning}
              className="w-full flex items-center justify-center gap-2 px-4 py-2 rounded-lg bg-sky-600 hover:bg-sky-500 disabled:bg-slate-800 text-white text-xs font-semibold shadow-md shadow-sky-600/20 transition duration-200"
            >
              {isRunning ? (
                <>
                  <RefreshCw className="w-4 h-4 animate-spin" />
                  <span>Executing 5 Policies...</span>
                </>
              ) : (
                <>
                  <Play className="w-4 h-4 fill-white" />
                  <span>Start Live Benchmark Suite</span>
                </>
              )}
            </button>
          </div>
        </form>

        {error && (
          <div className="mt-4 p-3 rounded-xl bg-rose-500/10 border border-rose-500/20 flex items-center gap-2 text-rose-400 text-xs">
            <AlertCircle className="w-4 h-4 flex-shrink-0" />
            <span>{error}</span>
          </div>
        )}
      </div>

      {/* Live Benchmark Execution Results */}
      {lastResult && (
        <div className="p-6 rounded-2xl bg-slate-900/60 border border-emerald-500/30">
          <div className="flex items-center gap-2 mb-4">
            <CheckCircle className="w-5 h-5 text-emerald-400" />
            <h3 className="text-sm font-bold text-white tracking-tight">
              Experiment Complete: #{lastResult.experiment_id} - {lastResult.experiment_name}
            </h3>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs font-mono">
              <thead>
                <tr className="border-b border-slate-800 text-slate-400">
                  <th className="pb-2 font-medium">Policy</th>
                  <th className="pb-2 font-medium">Hit Rate (%)</th>
                  <th className="pb-2 font-medium">Avg Latency</th>
                  <th className="pb-2 font-medium">Throughput</th>
                  <th className="pb-2 font-medium">Evictions</th>
                  <th className="pb-2 font-medium">Staleness</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-800/60">
                {lastResult.results.map((r: any) => (
                  <tr key={r.policy} className="hover:bg-slate-800/20">
                    <td className="py-2.5 font-bold text-white">{r.policy}</td>
                    <td className="py-2.5 text-emerald-400 font-semibold">{r.hit_rate}%</td>
                    <td className="py-2.5 text-slate-300">{r.avg_latency} ms</td>
                    <td className="py-2.5 text-purple-400">{r.throughput} rps</td>
                    <td className="py-2.5 text-rose-400">{r.total_evictions}</td>
                    <td className="py-2.5 text-amber-400">{r.staleness_sec} s</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
};
