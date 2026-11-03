import React, { useState, useEffect } from 'react';
import { DBMSStats, QueryPerformanceDemo } from '../types';
import { api } from '../services/api';
import { Database, Table, Zap, Terminal, Shield, CheckCheck } from 'lucide-react';

interface DBMSAnalyticsProps {
  stats: DBMSStats | null;
}

export const DBMSAnalyticsSection: React.FC<DBMSAnalyticsProps> = ({ stats }) => {
  const [demos, setDemos] = useState<QueryPerformanceDemo[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    loadDemos();
  }, []);

  const loadDemos = async () => {
    setLoading(true);
    try {
      const res = await api.getQueryPerformance();
      setDemos(res.demonstrations || []);
    } catch (e) {
      console.error(e);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="space-y-6">
      {/* DBMS Overview Banner */}
      <div className="p-6 rounded-2xl bg-slate-900/60 border border-slate-800">
        <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
          <div>
            <div className="flex items-center gap-2 mb-1">
              <Database className="w-5 h-5 text-sky-400" />
              <h2 className="text-base font-bold text-white tracking-tight">MySQL Relational DBMS Integration</h2>
              <span className="text-xs px-2.5 py-0.5 rounded-full font-mono bg-sky-500/10 text-sky-400 border border-sky-500/20">
                {stats?.rdbms_type || 'MySQL 8.x'}
              </span>
            </div>
            <p className="text-xs text-slate-400 max-w-2xl">
              Normalized relational persistence schema with 10 tables, primary/foreign key constraints,
              composite B-tree indexes, stored procedures, triggers, audit logging, and analytical views.
            </p>
          </div>

          <div className="text-right font-mono text-xs">
            <span className="text-slate-400 block">Total Relational Records</span>
            <span className="text-xl font-bold text-emerald-400">
              {stats?.total_records?.toLocaleString() || 0}
            </span>
          </div>
        </div>
      </div>

      {/* Relational Tables Grid */}
      <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-6 gap-3">
        {stats?.tables &&
          Object.entries(stats.tables).map(([tbl, count]) => (
            <div key={tbl} className="p-3.5 rounded-xl bg-slate-900/70 border border-slate-800 font-mono">
              <div className="flex items-center gap-1.5 text-slate-400 text-[11px] mb-1">
                <Table className="w-3.5 h-3.5 text-sky-400" />
                <span className="truncate" title={tbl}>
                  {tbl}
                </span>
              </div>
              <div className="text-base font-bold text-white">{count.toLocaleString()}</div>
            </div>
          ))}
      </div>

      {/* SQL View & Query Execution Benchmark Demonstrations */}
      <div className="p-6 rounded-2xl bg-slate-900/60 border border-slate-800">
        <div className="flex items-center justify-between mb-4">
          <div>
            <h3 className="text-sm font-semibold text-slate-200">SQL View & Query Performance Demonstrations</h3>
            <p className="text-xs text-slate-400">Execution latency across indexed queries and analytical views</p>
          </div>
          <button
            onClick={loadDemos}
            disabled={loading}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 text-xs font-mono border border-slate-700 transition"
          >
            <Zap className="w-3.5 h-3.5 text-amber-400" />
            <span>Benchmark Queries</span>
          </button>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs font-mono">
            <thead>
              <tr className="border-b border-slate-800 text-slate-400">
                <th className="pb-2.5 font-medium">Demonstration Name</th>
                <th className="pb-2.5 font-medium">SQL Expression</th>
                <th className="pb-2.5 font-medium">Row Count</th>
                <th className="pb-2.5 font-medium">Execution Time</th>
                <th className="pb-2.5 font-medium">Index Status</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-800/60">
              {demos.map((d, i) => (
                <tr key={i} className="hover:bg-slate-800/20">
                  <td className="py-2.5 font-bold text-white">{d.query_name}</td>
                  <td className="py-2.5 text-slate-300">
                    <code className="text-sky-300 bg-slate-950 px-1.5 py-0.5 rounded text-[11px]">{d.sql}</code>
                  </td>
                  <td className="py-2.5 text-slate-300">{d.row_count} rows</td>
                  <td className="py-2.5 font-semibold text-emerald-400">{d.execution_time_ms} ms</td>
                  <td className="py-2.5">
                    <span className="flex items-center gap-1 text-[10px] text-emerald-400">
                      <CheckCheck className="w-3 h-3" />
                      <span>{d.status}</span>
                    </span>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
};
