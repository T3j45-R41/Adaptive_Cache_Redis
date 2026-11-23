import React, { useState, useEffect } from 'react';
import { Header } from './components/Header';
import { OverviewSection } from './components/OverviewSection';
import { ComparisonMatrixSection } from './components/ComparisonMatrixSection';
import { AdaptiveMonitoringSection } from './components/AdaptiveMonitoringSection';
import { StockMarketSection } from './components/StockMarketSection';
import { BenchmarkControlSection } from './components/BenchmarkControlSection';
import { DBMSAnalyticsSection } from './components/DBMSAnalyticsSection';
import { api } from './services/api';
import { DashboardOverview, BenchmarkComparisonItem, StockQuote, DBMSStats } from './types';
import { RefreshCw } from 'lucide-react';

export const App: React.FC = () => {
  const [activeTab, setActiveTab] = useState<string>('overview');
  const [isRefreshing, setIsRefreshing] = useState<boolean>(false);
  const [overview, setOverview] = useState<DashboardOverview | null>(null);
  const [comparison, setComparison] = useState<BenchmarkComparisonItem[]>([]);
  const [stocks, setStocks] = useState<StockQuote[]>([]);
  const [dbmsStats, setDBMSStats] = useState<DBMSStats | null>(null);
  const [adaptiveHistory, setAdaptiveHistory] = useState<any[]>([]);

  const loadAllData = async () => {
    setIsRefreshing(true);
    try {
      const [ov, comp, stk, db, adapt] = await Promise.allSettled([
        api.getDashboardOverview(),
        api.getComparisonMatrix(),
        api.getStocks(),
        api.getDBMSStats(),
        api.getAdaptiveWeights(),
      ]);

      if (ov.status === 'fulfilled') setOverview(ov.value);
      if (comp.status === 'fulfilled') setComparison(comp.value);
      if (stk.status === 'fulfilled') setStocks(stk.value);
      if (db.status === 'fulfilled') setDBMSStats(db.value);
      if (adapt.status === 'fulfilled') setAdaptiveHistory(adapt.value);
    } catch (e) {
      console.error('Error fetching dashboard data:', e);
    } finally {
      setIsRefreshing(false);
    }
  };

  useEffect(() => {
    loadAllData();
    // Auto-refresh every 10 seconds
    const interval = setInterval(loadAllData, 10000);
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="min-h-screen bg-ctp-base text-ctp-text flex flex-col font-sans selection:bg-ctp-lavender/30">
      <Header
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        onRefresh={loadAllData}
        isRefreshing={isRefreshing}
        status={overview?.status || 'connecting'}
      />

      <main className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 py-8">
        {activeTab === 'overview' && (
          overview ? (
            <OverviewSection data={overview} onNavigateTab={setActiveTab} />
          ) : (
            <div className="p-8 rounded-2xl bg-white border border-ctp-surface0 shadow-xs text-center space-y-4">
              <div className="flex justify-center">
                <RefreshCw className={`w-8 h-8 text-ctp-blue ${isRefreshing ? 'animate-spin' : ''}`} />
              </div>
              <div>
                <h2 className="text-base font-bold text-ctp-text">Connecting to Backend API</h2>
                <p className="text-xs text-ctp-subtext0 mt-1 max-w-md mx-auto">
                  Attempting to communicate with the FastAPI server at <code className="text-ctp-blue font-mono">http://127.0.0.1:8000</code>.
                </p>
              </div>
              <div className="p-4 rounded-xl bg-ctp-mantle border border-ctp-surface1 max-w-lg mx-auto text-left text-xs font-mono space-y-1.5 text-ctp-subtext1">
                <div className="text-ctp-text font-semibold mb-1">To launch all backend services:</div>
                <div>1. Double click: <code className="text-ctp-blue font-bold">run_all.bat</code> in the project root</div>
                <div>2. Or in terminal: <code className="text-ctp-teal">python -m uvicorn backend.app.main:app --port 8000</code></div>
              </div>
              <button
                onClick={loadAllData}
                disabled={isRefreshing}
                className="px-4 py-2 rounded-lg bg-ctp-blue hover:bg-ctp-sapphire text-white text-xs font-semibold shadow-xs transition disabled:opacity-50"
              >
                {isRefreshing ? 'Connecting...' : 'Retry Connection Now'}
              </button>
            </div>
          )
        )}

        {activeTab === 'comparison' && (
          <ComparisonMatrixSection data={comparison} />
        )}

        {activeTab === 'adaptive' && overview && (
          <AdaptiveMonitoringSection
            overview={overview}
            weightsHistory={adaptiveHistory}
          />
        )}

        {activeTab === 'stocks' && (
          <StockMarketSection stocks={stocks} />
        )}

        {activeTab === 'benchmark' && (
          <BenchmarkControlSection onExperimentComplete={loadAllData} />
        )}

        {activeTab === 'dbms' && (
          <DBMSAnalyticsSection stats={dbmsStats} />
        )}
      </main>

      <footer className="border-t border-ctp-surface0 bg-ctp-mantle py-4 text-center text-xs text-ctp-subtext0 font-mono">
        Adaptive Cache Eviction & MySQL DBMS • University PBL Project Demonstration
      </footer>
    </div>
  );
};

export default App;
