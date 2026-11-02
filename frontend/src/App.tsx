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
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col font-sans">
      <Header
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        onRefresh={loadAllData}
        isRefreshing={isRefreshing}
        status={overview?.status || 'connecting'}
      />

      <main className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 py-8">
        {activeTab === 'overview' && overview && (
          <OverviewSection data={overview} onNavigateTab={setActiveTab} />
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

      <footer className="border-t border-slate-900 bg-slate-950 py-4 text-center text-xs text-slate-500 font-mono">
        Adaptive Cache Eviction & MySQL DBMS • University PBL Project Demonstration
      </footer>
    </div>
  );
};

export default App;
