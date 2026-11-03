import React from 'react';
import { Database, Cpu, Activity, RefreshCw } from 'lucide-react';

interface HeaderProps {
  activeTab: string;
  setActiveTab: (tab: string) => void;
  onRefresh: () => void;
  isRefreshing: boolean;
  status: string;
}

export const Header: React.FC<HeaderProps> = ({
  activeTab,
  setActiveTab,
  onRefresh,
  isRefreshing,
  status
}) => {
  const tabs = [
    { id: 'overview', label: 'Overview' },
    { id: 'comparison', label: 'Policy Matrix' },
    { id: 'adaptive', label: 'Adaptive CACHEUS' },
    { id: 'stocks', label: 'Stock Monitoring' },
    { id: 'benchmark', label: 'Experiment Runner' },
    { id: 'dbms', label: 'MySQL DBMS' },
  ];

  return (
    <header className="border-b border-ctp-surface0 bg-ctp-mantle/90 backdrop-blur sticky top-0 z-50 shadow-xs">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex items-center justify-between h-16">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-gradient-to-tr from-ctp-blue to-ctp-lavender flex items-center justify-center shadow-md shadow-ctp-blue/20">
              <Cpu className="w-6 h-6 text-white" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <span className="font-bold text-lg text-ctp-text tracking-tight">AdaptiveCache</span>
                <span className="text-xs px-2 py-0.5 rounded-full font-mono font-medium bg-ctp-blue/10 text-ctp-blue border border-ctp-blue/25">
                  Redis-Compatible
                </span>
              </div>
              <p className="text-xs text-ctp-subtext0">Stock Quote Eviction & MySQL DBMS</p>
            </div>
          </div>

          <div className="flex items-center gap-4">
            <div className="flex items-center gap-2 px-3 py-1 rounded-full bg-ctp-surface0/70 border border-ctp-surface1 text-xs font-mono">
              <span className={`w-2 h-2 rounded-full ${status === 'healthy' ? 'bg-ctp-green animate-pulse' : 'bg-ctp-peach'}`} />
              <span className="text-ctp-subtext1">Server: {status}</span>
            </div>

            <button
              onClick={onRefresh}
              disabled={isRefreshing}
              className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-ctp-surface0 hover:bg-ctp-surface1 text-ctp-text text-xs font-medium border border-ctp-surface1 transition shadow-xs disabled:opacity-50"
              title="Refresh Data"
            >
              <RefreshCw className={`w-3.5 h-3.5 ${isRefreshing ? 'animate-spin text-ctp-blue' : ''}`} />
              <span>Refresh</span>
            </button>
          </div>
        </div>

        {/* Tab Navigation */}
        <div className="flex space-x-1 overflow-x-auto py-2 border-t border-ctp-surface0/60 no-scrollbar">
          {tabs.map((tab) => (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id)}
              className={`px-3.5 py-1.5 rounded-lg text-xs font-medium whitespace-nowrap transition-all duration-200 ${
                activeTab === tab.id
                  ? 'bg-ctp-blue/15 text-ctp-blue border border-ctp-blue/30 shadow-xs font-semibold'
                  : 'text-ctp-subtext0 hover:text-ctp-text hover:bg-ctp-surface0/60'
              }`}
            >
              {tab.label}
            </button>
          ))}
        </div>
      </div>
    </header>
  );
};
