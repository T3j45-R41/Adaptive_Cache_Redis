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
      <div className="p-6 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
        <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
          <div>
            <div className="flex items-center gap-2 mb-1">
              <Database className="w-5 h-5 text-ctp-blue" />
              <h2 className="text-base font-bold text-ctp-text tracking-tight">MySQL Relational DBMS Integration</h2>
              <span className="text-xs px-2.5 py-0.5 rounded-full font-mono bg-ctp-blue/10 text-ctp-blue border border-ctp-blue/25 font-medium">
                {stats?.rdbms_type || 'MySQL 8.x'}
              </span>
            </div>
            <p className="text-xs text-ctp-subtext0 max-w-2xl">
              Normalized relational persistence schema with 10 tables, primary/foreign key constraints,
              composite B-tree indexes, stored procedures, triggers, audit logging, and analytical views.
            </p>
          </div>

          <div className="text-right font-mono text-xs">
            <span className="text-ctp-subtext0 block">Total Relational Records</span>
            <span className="text-xl font-bold text-ctp-green">
              {stats?.total_records?.toLocaleString() || 0}
            </span>
          </div>
        </div>
      </div>

      {/* Relational Tables Grid */}
      <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-6 gap-3">
        {stats?.tables &&
          Object.entries(stats.tables).map(([tbl, count]) => (
            <div key={tbl} className="p-3.5 rounded-xl bg-white border border-ctp-surface0 font-mono shadow-xs">
              <div className="flex items-center gap-1.5 text-ctp-subtext0 text-[11px] mb-1">
                <Table className="w-3.5 h-3.5 text-ctp-blue" />
                <span className="truncate" title={tbl}>
                  {tbl}
                </span>
              </div>
              <div className="text-base font-bold text-ctp-text">{count.toLocaleString()}</div>
            </div>
          ))}
      </div>

      {/* SQL View & Query Execution Benchmark Demonstrations */}
      <div className="p-6 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
        <div className="flex items-center justify-between mb-4">
          <div>
            <h3 className="text-sm font-semibold text-ctp-text">SQL View & Query Performance Demonstrations</h3>
            <p className="text-xs text-ctp-subtext0">Execution latency across indexed queries and analytical views</p>
          </div>
          <button
            onClick={loadDemos}
            disabled={loading}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-ctp-surface0 hover:bg-ctp-surface1 text-ctp-text text-xs font-mono border border-ctp-surface1 transition shadow-xs disabled:opacity-50"
          >
            <Zap className={`w-3.5 h-3.5 text-ctp-peach ${loading ? 'animate-bounce' : ''}`} />
            <span>Benchmark Queries</span>
          </button>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs font-mono">
            <thead>
              <tr className="border-b border-ctp-surface0 text-ctp-subtext0">
                <th className="pb-2.5 font-medium">Demonstration Name</th>
                <th className="pb-2.5 font-medium">SQL Expression</th>
                <th className="pb-2.5 font-medium">Row Count</th>
                <th className="pb-2.5 font-medium">Execution Time</th>
                <th className="pb-2.5 font-medium">Index Status</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-ctp-surface0/60">
              {demos.map((d, i) => (
                <tr key={i} className="hover:bg-ctp-mantle/70 transition">
                  <td className="py-2.5 font-bold text-ctp-text">{d.query_name}</td>
                  <td className="py-2.5 text-ctp-subtext1">
                    <code className="text-ctp-blue bg-ctp-mantle border border-ctp-surface0 px-1.5 py-0.5 rounded text-[11px]">{d.sql}</code>
                  </td>
                  <td className="py-2.5 text-ctp-subtext1">{d.row_count} rows</td>
                  <td className="py-2.5 font-semibold text-ctp-green">{d.execution_time_ms} ms</td>
                  <td className="py-2.5">
                    <span className="flex items-center gap-1 text-[10px] text-ctp-green font-medium">
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
