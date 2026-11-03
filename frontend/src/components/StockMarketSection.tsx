import React from 'react';
import { StockQuote } from '../types';
import { TrendingUp, Clock, CheckCircle2, XCircle, Database } from 'lucide-react';

interface StockMarketSectionProps {
  stocks: StockQuote[];
}

export const StockMarketSection: React.FC<StockMarketSectionProps> = ({ stocks }) => {
  return (
    <div className="space-y-6">
      {/* Header Info Banner */}
      <div className="p-5 rounded-2xl bg-slate-900/60 border border-slate-800 flex items-center justify-between">
        <div>
          <h2 className="text-base font-bold text-white tracking-tight">Stock Quote Cache & Staleness Monitor</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Real-time quote serving via in-memory C++ cache vs MySQL persistent records
          </p>
        </div>
        <div className="text-xs font-mono text-slate-400">
          Tracking <span className="font-bold text-white">{stocks.length}</span> Ticker Instruments
        </div>
      </div>

      {/* Ticker Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
        {stocks.map((stock) => (
          <div
            key={stock.symbol}
            className="p-4 rounded-xl bg-slate-900/70 border border-slate-800 shadow-sm hover:border-slate-700 transition"
          >
            {/* Symbol & Cache Status */}
            <div className="flex items-center justify-between mb-2">
              <span className="font-bold font-mono text-base text-white tracking-wide">{stock.symbol}</span>
              {stock.is_cached ? (
                <span className="flex items-center gap-1 text-[11px] px-2 py-0.5 rounded-full font-mono bg-emerald-500/15 text-emerald-400 border border-emerald-500/30">
                  <CheckCircle2 className="w-3 h-3" />
                  <span>Cached</span>
                </span>
              ) : (
                <span className="flex items-center gap-1 text-[11px] px-2 py-0.5 rounded-full font-mono bg-slate-800 text-slate-400 border border-slate-700">
                  <XCircle className="w-3 h-3" />
                  <span>Cache Miss</span>
                </span>
              )}
            </div>

            {/* Price Comparison */}
            <div className="space-y-1.5 my-3 text-xs font-mono">
              <div className="flex justify-between items-center">
                <span className="text-slate-400">Source Price:</span>
                <span className="font-semibold text-white">${stock.source_price.toFixed(2)}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-slate-400">Cached Price:</span>
                <span className={stock.is_cached ? 'font-semibold text-emerald-400' : 'text-slate-500'}>
                  {stock.cached_price ? `$${stock.cached_price.toFixed(2)}` : 'N/A'}
                </span>
              </div>
            </div>

            {/* Timestamps & Staleness */}
            <div className="pt-2.5 border-t border-slate-800/80 text-[11px] space-y-1 font-mono">
              <div className="flex justify-between text-slate-400">
                <span>Source Time:</span>
                <span className="text-slate-300">{stock.market_timestamp.slice(11, 19)}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-slate-400">Staleness:</span>
                <span
                  className={`px-1.5 py-0.2 rounded font-semibold ${
                    stock.staleness_seconds > 60
                      ? 'bg-amber-500/20 text-amber-400'
                      : 'bg-emerald-500/20 text-emerald-400'
                  }`}
                >
                  {stock.is_cached ? `${stock.staleness_seconds.toFixed(1)}s` : 'N/A'}
                </span>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
