import React, { useState, useEffect } from 'react';
import { DBMSStats, QueryPerformanceDemo } from '../types';
import { api } from '../services/api';
import {
  Database,
  Table,
  Zap,
  Terminal,
  Shield,
  CheckCheck,
  AlertCircle,
  RefreshCw,
  Play,
  Copy,
  Check,
  Eye,
  Code,
  Layers,
  Search,
} from 'lucide-react';

interface DBMSAnalyticsProps {
  stats: DBMSStats | null;
}

export const DBMSAnalyticsSection: React.FC<DBMSAnalyticsProps> = ({ stats }) => {
  const [demos, setDemos] = useState<QueryPerformanceDemo[]>([]);
  const [engine, setEngine] = useState<string>('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Selected query output inspection state
  const [selectedQuery, setSelectedQuery] = useState<QueryPerformanceDemo | null>(null);
  const [activeTable, setActiveTable] = useState<string | null>(null);
  const [customSql, setCustomSql] = useState<string>('SELECT * FROM stock_data WHERE symbol = \'AAPL\' LIMIT 10');
  const [executingCustom, setExecutingCustom] = useState<boolean>(false);
  const [customError, setCustomError] = useState<string | null>(null);
  const [copied, setCopied] = useState<boolean>(false);

  useEffect(() => {
    loadDemos();
  }, []);

  const loadDemos = async () => {
    setLoading(true);
    setError(null);
    try {
      const res = await api.getQueryPerformance();
      const list = res.demonstrations || [];
      setDemos(list);
      if (res.engine) setEngine(res.engine);

      // Default select the first demo if none selected
      if (list.length > 0 && !selectedQuery) {
        setSelectedQuery(list[0]);
      }
    } catch (e: any) {
      console.error(e);
      setError('Unable to fetch benchmark query results. Ensure the FastAPI backend is running on port 8000.');
    } finally {
      setLoading(false);
    }
  };

  const handleSelectTable = async (tableName: string) => {
    setActiveTable(tableName);
    setLoading(true);
    setCustomError(null);
    try {
      const res = await api.getTableData(tableName, 25);
      setSelectedQuery(res);
      setCustomSql(res.sql);
    } catch (e: any) {
      console.error(e);
      setCustomError(e.message || `Failed to fetch records for table ${tableName}`);
    } finally {
      setLoading(false);
    }
  };

  const handleRunCustomQuery = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    if (!customSql.trim()) return;

    setExecutingCustom(true);
    setCustomError(null);
    try {
      const res = await api.executeCustomQuery(customSql.trim());
      setSelectedQuery(res);
    } catch (e: any) {
      setCustomError(e.message || 'SQL execution failed');
    } finally {
      setExecutingCustom(false);
    }
  };

  const handleCopySql = (sql: string) => {
    navigator.clipboard.writeText(sql);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const presetQueries = [
    { label: 'vw_policy_performance', sql: 'SELECT * FROM vw_policy_performance' },
    { label: 'vw_experiment_comparison', sql: 'SELECT * FROM vw_experiment_comparison LIMIT 20' },
    { label: 'Partition Pruned Stocks', sql: "SELECT * FROM stock_data WHERE market_timestamp >= '2026-10-01 00:00:00' AND market_timestamp < '2027-01-01 00:00:00' LIMIT 15" },
    { label: 'Indexed Symbol Lookup', sql: "SELECT * FROM stock_data WHERE symbol = 'AAPL' LIMIT 15" },
    { label: 'vw_stock_history', sql: 'SELECT * FROM vw_stock_history' },
    { label: 'vw_eviction_statistics', sql: 'SELECT * FROM vw_eviction_statistics' },
    { label: 'Audit Trail Retrieval', sql: 'SELECT * FROM experiment_audit_log ORDER BY changed_at DESC LIMIT 15' },
    { label: 'Explain Partition Pruning', sql: "EXPLAIN SELECT * FROM stock_data WHERE market_timestamp >= '2026-10-01 00:00:00' AND market_timestamp < '2027-01-01 00:00:00'" },
  ];

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
                {stats?.rdbms_type || engine || 'MySQL 8.x'}
              </span>
            </div>
            <p className="text-xs text-ctp-subtext0 max-w-2xl">
              Normalized relational schema with 10 tables, primary/foreign key constraints, composite B-tree indexes,
              stored procedures, triggers, audit logging, analytical views, and time-series table partitioning.
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

      {/* Relational Tables Grid (Interactive) */}
      <div>
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-1.5 text-xs font-semibold text-ctp-subtext1">
            <Table className="w-3.5 h-3.5 text-ctp-blue" />
            <span>Click any table to preview live database records:</span>
          </div>
          {activeTable && (
            <span className="text-[11px] font-mono text-ctp-blue bg-ctp-blue/10 px-2 py-0.5 rounded-md border border-ctp-blue/20">
              Active: {activeTable}
            </span>
          )}
        </div>
        <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-6 gap-3">
          {stats?.tables &&
            Object.entries(stats.tables).map(([tbl, count]) => {
              const isSelected = activeTable === tbl;
              return (
                <button
                  key={tbl}
                  onClick={() => handleSelectTable(tbl)}
                  className={`p-3.5 rounded-xl text-left font-mono shadow-xs transition duration-150 border ${isSelected
                      ? 'bg-ctp-blue/10 border-ctp-blue shadow-sm'
                      : 'bg-white border-ctp-surface0 hover:border-ctp-blue/40 hover:bg-ctp-mantle/50'
                    }`}
                >
                  <div className="flex items-center gap-1.5 text-ctp-subtext0 text-[11px] mb-1">
                    <Table className={`w-3.5 h-3.5 ${isSelected ? 'text-ctp-blue' : 'text-ctp-subtext1'}`} />
                    <span className="truncate font-semibold" title={tbl}>
                      {tbl}
                    </span>
                  </div>
                  <div className="text-base font-bold text-ctp-text">{count.toLocaleString()}</div>
                </button>
              );
            })}
        </div>
      </div>

      {/* SQL View & Query Performance Demonstrations Table */}
      <div className="p-6 rounded-2xl bg-white border border-ctp-surface0 shadow-xs">
        <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3 mb-4">
          <div>
            <div className="flex items-center gap-2">
              <h3 className="text-sm font-semibold text-ctp-text">SQL View & Query Performance Demonstrations</h3>
              {engine && (
                <span className="text-[10px] px-2 py-0.5 rounded-full font-mono bg-ctp-lavender/10 text-ctp-lavender border border-ctp-lavender/20 font-medium">
                  {engine}
                </span>
              )}
            </div>
            <p className="text-xs text-ctp-subtext0 mt-0.5">
              Click any demonstration row to inspect its live returned dataset in the table viewer below
            </p>
          </div>
          <button
            onClick={loadDemos}
            disabled={loading}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-ctp-surface0 hover:bg-ctp-surface1 text-ctp-text text-xs font-mono border border-ctp-surface1 transition shadow-xs disabled:opacity-50"
          >
            <Zap className={`w-3.5 h-3.5 text-ctp-peach ${loading ? 'animate-bounce' : ''}`} />
            <span>{loading ? 'Benchmarking...' : 'Benchmark Queries'}</span>
          </button>
        </div>

        {error && (
          <div className="mb-4 p-3 rounded-xl bg-ctp-red/10 border border-ctp-red/20 flex items-center justify-between text-xs text-ctp-red">
            <div className="flex items-center gap-2">
              <AlertCircle className="w-4 h-4 flex-shrink-0" />
              <span>{error}</span>
            </div>
            <button
              onClick={loadDemos}
              className="underline hover:text-ctp-maroon font-semibold ml-2 flex-shrink-0"
            >
              Retry
            </button>
          </div>
        )}

        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs font-mono">
            <thead>
              <tr className="border-b border-ctp-surface0 text-ctp-subtext0">
                <th className="pb-2.5 font-medium">Demonstration Name</th>
                <th className="pb-2.5 font-medium">SQL Expression</th>
                <th className="pb-2.5 font-medium">Row Count</th>
                <th className="pb-2.5 font-medium">Execution Time</th>
                <th className="pb-2.5 font-medium">Index Status</th>
                <th className="pb-2.5 font-medium text-right">Inspect Output</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-ctp-surface0/60">
              {loading && demos.length === 0 ? (
                <tr>
                  <td colSpan={6} className="py-8 text-center text-ctp-subtext0">
                    <div className="flex items-center justify-center gap-2">
                      <RefreshCw className="w-4 h-4 animate-spin text-ctp-blue" />
                      <span>Executing relational query benchmark suite...</span>
                    </div>
                  </td>
                </tr>
              ) : error && demos.length === 0 ? (
                <tr>
                  <td colSpan={6} className="py-8 text-center text-ctp-subtext0">
                    <p className="font-semibold text-ctp-text mb-1">Backend service is currently unavailable.</p>
                    <p className="text-[11px] text-ctp-subtext1">
                      Start the FastAPI server (<code>python -m uvicorn backend.app.main:app --port 8000</code> or via <code>run_all.bat</code>) and click <strong className="text-ctp-blue font-semibold">"Benchmark Queries"</strong>.
                    </p>
                  </td>
                </tr>
              ) : demos.length === 0 ? (
                <tr>
                  <td colSpan={6} className="py-8 text-center text-ctp-subtext0">
                    <p className="font-semibold text-ctp-text mb-1">No benchmarked query results loaded.</p>
                    <p className="text-[11px] text-ctp-subtext1">
                      Click the <strong className="text-ctp-blue font-semibold">"Benchmark Queries"</strong> button above to measure execution latencies across views and indexes.
                    </p>
                  </td>
                </tr>
              ) : (
                demos.map((d, i) => {
                  const isSelected = selectedQuery?.query_name === d.query_name;
                  return (
                    <tr
                      key={i}
                      onClick={() => {
                        setSelectedQuery(d);
                        setActiveTable(null);
                        setCustomSql(d.sql);
                      }}
                      className={`cursor-pointer transition ${isSelected
                          ? 'bg-ctp-blue/10 border-l-4 border-l-ctp-blue font-semibold'
                          : 'hover:bg-ctp-mantle/70'
                        }`}
                    >
                      <td className="py-2.5 px-2 font-bold text-ctp-text">{d.query_name}</td>
                      <td className="py-2.5 text-ctp-subtext1 max-w-xs truncate" title={d.sql}>
                        <code className="text-ctp-blue bg-ctp-mantle border border-ctp-surface0 px-1.5 py-0.5 rounded text-[11px]">
                          {d.sql}
                        </code>
                      </td>
                      <td className="py-2.5 text-ctp-subtext1">{d.row_count} rows</td>
                      <td className="py-2.5 font-semibold text-ctp-green">{d.execution_time_ms} ms</td>
                      <td className="py-2.5">
                        <span className="flex items-center gap-1 text-[10px] text-ctp-green font-medium">
                          <CheckCheck className="w-3 h-3" />
                          <span>{d.status}</span>
                        </span>
                      </td>
                      <td className="py-2.5 pr-2 text-right">
                        <button
                          onClick={(e) => {
                            e.stopPropagation();
                            setSelectedQuery(d);
                            setActiveTable(null);
                            setCustomSql(d.sql);
                          }}
                          className={`px-2 py-1 rounded text-[11px] font-sans font-medium transition flex items-center gap-1 ml-auto ${isSelected
                              ? 'bg-ctp-blue text-white shadow-xs'
                              : 'bg-ctp-surface0 hover:bg-ctp-blue hover:text-white text-ctp-subtext1'
                            }`}
                        >
                          <Eye className="w-3 h-3" />
                          <span>{isSelected ? 'Viewing' : 'View Table'}</span>
                        </button>
                      </td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
      </div>

      {/* Live MySQL Query Output Inspector & Table Viewer */}
      <div className="p-6 rounded-2xl bg-white border border-ctp-blue/30 shadow-xs space-y-5">
        <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-3">
          <div>
            <div className="flex items-center gap-2">
              <Terminal className="w-5 h-5 text-ctp-blue" />
              <h3 className="text-base font-bold text-ctp-text tracking-tight">
                Live Query Output & Relational Result Inspector
              </h3>
            </div>
            <p className="text-xs text-ctp-subtext0 mt-0.5">
              Live database records returned by MySQL query execution with column headers and active row counts
            </p>
          </div>

          {selectedQuery && (
            <div className="flex flex-wrap items-center gap-2">
              <span className="text-xs px-2.5 py-1 rounded-lg font-mono bg-ctp-surface0 text-ctp-text border border-ctp-surface1">
                <strong>{selectedQuery.query_name || 'Active Query'}</strong>
              </span>
              <span className="text-xs px-2.5 py-1 rounded-lg font-mono bg-ctp-green/10 text-ctp-green border border-ctp-green/30 font-semibold">
                ⚡ {selectedQuery.execution_time_ms} ms
              </span>
              <span className="text-xs px-2.5 py-1 rounded-lg font-mono bg-ctp-blue/10 text-ctp-blue border border-ctp-blue/30 font-semibold">
                {selectedQuery.row_count} rows
              </span>
            </div>
          )}
        </div>

        {/* Quick Query Preset Pills */}
        <div>
          <span className="block text-[11px] font-medium text-ctp-subtext0 mb-1.5">
            Quick Preset Queries (Click to execute and inspect):
          </span>
          <div className="flex flex-wrap gap-1.5">
            {presetQueries.map((pq, idx) => (
              <button
                key={idx}
                onClick={async () => {
                  setCustomSql(pq.sql);
                  setExecutingCustom(true);
                  setCustomError(null);
                  try {
                    const res = await api.executeCustomQuery(pq.sql);
                    setSelectedQuery({ ...res, query_name: pq.label });
                    setActiveTable(null);
                  } catch (err: any) {
                    setCustomError(err.message || 'Execution failed');
                  } finally {
                    setExecutingCustom(false);
                  }
                }}
                className={`text-xs px-2.5 py-1 rounded-lg font-mono transition border ${customSql === pq.sql
                    ? 'bg-ctp-blue text-white border-ctp-blue shadow-xs font-semibold'
                    : 'bg-ctp-mantle text-ctp-subtext1 border-ctp-surface1 hover:border-ctp-blue hover:text-ctp-blue'
                  }`}
              >
                {pq.label}
              </button>
            ))}
          </div>
        </div>

        {/* SQL Expression Code Box with Copy & Run */}
        <div className="rounded-xl bg-ctp-mantle border border-ctp-surface1 overflow-hidden">
          <div className="flex items-center justify-between px-3 py-1.5 bg-ctp-surface0/60 border-b border-ctp-surface1 text-xs font-mono text-ctp-subtext1">
            <span className="flex items-center gap-1.5">
              <Code className="w-3.5 h-3.5 text-ctp-blue" />
              <span>SQL Statement</span>
            </span>
            <button
              onClick={() => handleCopySql(customSql)}
              className="flex items-center gap-1 text-[11px] text-ctp-subtext0 hover:text-ctp-text transition"
              title="Copy SQL to Clipboard"
            >
              {copied ? <Check className="w-3 h-3 text-ctp-green" /> : <Copy className="w-3 h-3" />}
              <span>{copied ? 'Copied!' : 'Copy SQL'}</span>
            </button>
          </div>
          <form onSubmit={handleRunCustomQuery} className="p-3 space-y-2">
            <div className="flex gap-2">
              <input
                type="text"
                value={customSql}
                onChange={(e) => setCustomSql(e.target.value)}
                placeholder="Enter SQL statement (e.g. SELECT * FROM stock_data WHERE symbol = 'AAPL' LIMIT 10)"
                className="flex-1 bg-white border border-ctp-surface1 rounded-lg px-3 py-2 text-xs font-mono text-ctp-text focus:ring-1 focus:ring-ctp-blue focus:border-ctp-blue focus:outline-none transition"
              />
              <button
                type="submit"
                disabled={executingCustom}
                className="flex items-center gap-1.5 px-4 py-2 rounded-lg bg-ctp-blue hover:bg-ctp-sapphire disabled:bg-ctp-surface1 text-white text-xs font-semibold transition shadow-xs flex-shrink-0"
              >
                {executingCustom ? (
                  <>
                    <RefreshCw className="w-3.5 h-3.5 animate-spin" />
                    <span>Executing...</span>
                  </>
                ) : (
                  <>
                    <Play className="w-3.5 h-3.5 fill-white" />
                    <span>Run Query</span>
                  </>
                )}
              </button>
            </div>
            {customError && (
              <div className="p-2.5 rounded-lg bg-ctp-red/10 border border-ctp-red/20 text-ctp-red text-xs flex items-center gap-2">
                <AlertCircle className="w-3.5 h-3.5 flex-shrink-0" />
                <span>{customError}</span>
              </div>
            )}
          </form>
        </div>

        {/* The Live Tabular Data Output */}
        <div className="rounded-xl border border-ctp-surface0 overflow-hidden bg-white shadow-2xs">
          <div className="flex items-center justify-between px-4 py-2.5 bg-ctp-mantle/70 border-b border-ctp-surface0 text-xs">
            <div className="flex items-center gap-2">
              <Table className="w-4 h-4 text-ctp-blue" />
              <span className="font-semibold text-ctp-text">Query Result Set:</span>
              <span className="font-mono text-ctp-subtext0">
                {selectedQuery?.rows?.length || 0} displayed (out of {selectedQuery?.row_count || 0} total)
              </span>
            </div>
            <span className="text-[11px] font-mono text-ctp-subtext1">
              Engine: {selectedQuery?.engine || engine || 'MySQL 8.x'}
            </span>
          </div>

          <div className="overflow-x-auto max-h-96">
            {selectedQuery && selectedQuery.columns && selectedQuery.columns.length > 0 && selectedQuery.rows && selectedQuery.rows.length > 0 ? (
              <table className="w-full text-left text-xs font-mono">
                <thead className="sticky top-0 bg-ctp-surface0/90 backdrop-blur border-b border-ctp-surface1 text-ctp-text">
                  <tr>
                    <th className="py-2 px-3 text-[11px] font-bold text-ctp-subtext0 w-12">#</th>
                    {selectedQuery.columns.map((col) => (
                      <th key={col} className="py-2 px-3 font-semibold whitespace-nowrap text-ctp-text">
                        {col}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody className="divide-y divide-ctp-surface0/60">
                  {selectedQuery.rows.map((row, rIdx) => (
                    <tr key={rIdx} className="hover:bg-ctp-mantle/60 transition">
                      <td className="py-2 px-3 text-ctp-subtext0 text-[11px] font-medium">{rIdx + 1}</td>
                      {selectedQuery.columns!.map((col) => {
                        const val = row[col];
                        let formatted = val === null || val === undefined ? 'NULL' : String(val);
                        const isNull = val === null || val === undefined;
                        const isNum = typeof val === 'number';

                        return (
                          <td
                            key={col}
                            className={`py-2 px-3 whitespace-nowrap truncate max-w-xs ${isNull
                                ? 'text-ctp-overlay1 italic'
                                : isNum
                                  ? 'text-ctp-blue font-semibold'
                                  : 'text-ctp-text'
                              }`}
                            title={formatted}
                          >
                            {formatted}
                          </td>
                        );
                      })}
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <div className="py-12 text-center text-ctp-subtext0 space-y-2">
                <Table className="w-8 h-8 mx-auto text-ctp-surface1" />
                <p className="font-semibold text-ctp-text text-xs">No records to display for this query.</p>
                <p className="text-[11px] text-ctp-subtext1">
                  Ensure the database has executed seed data or click on one of the quick preset queries above.
                </p>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

export default DBMSAnalyticsSection;
