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
      <div className="p-5 rounded-2xl bg-white border border-ctp-surface0 shadow-xs flex items-center justify-between">
        <div>
          <h2 className="text-base font-bold text-ctp-text tracking-tight">Stock Quote Cache & Staleness Monitor</h2>
          <p className="text-xs text-ctp-subtext0 mt-0.5">
            Real-time quote serving via in-memory C++ cache vs MySQL persistent records
          </p>
        </div>
        <div className="text-xs font-mono text-ctp-subtext0">
          Tracking <span className="font-bold text-ctp-text">{stocks.length}</span> Ticker Instruments
        </div>
      </div>

      {/* Ticker Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
        {stocks.map((stock) => (
          <div
            key={stock.symbol}
            className="p-4 rounded-xl bg-white border border-ctp-surface0 shadow-xs hover:border-ctp-surface1 hover:shadow-sm transition"
          >
            {/* Symbol & Cache Status */}
            <div className="flex items-center justify-between mb-2">
              <span className="font-bold font-mono text-base text-ctp-text tracking-wide">{stock.symbol}</span>
              {stock.is_cached ? (
                <span className="flex items-center gap-1 text-[11px] px-2 py-0.5 rounded-full font-mono bg-ctp-green/15 text-ctp-green border border-ctp-green/30 font-medium">
                  <CheckCircle2 className="w-3 h-3" />
                  <span>Cached</span>
                </span>
              ) : (
                <span className="flex items-center gap-1 text-[11px] px-2 py-0.5 rounded-full font-mono bg-ctp-surface0/60 text-ctp-subtext0 border border-ctp-surface1">
                  <XCircle className="w-3 h-3" />
                  <span>Cache Miss</span>
                </span>
              )}
            </div>

            {/* Price Comparison */}
            <div className="space-y-1.5 my-3 text-xs font-mono">
              <div className="flex justify-between items-center">
                <span className="text-ctp-subtext0">Source Price:</span>
                <span className="font-semibold text-ctp-text">${stock.source_price.toFixed(2)}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-ctp-subtext0">Cached Price:</span>
                <span className={stock.is_cached ? 'font-semibold text-ctp-green' : 'text-ctp-overlay1'}>
                  {stock.cached_price ? `$${stock.cached_price.toFixed(2)}` : 'N/A'}
                </span>
              </div>
            </div>

            {/* Timestamps & Staleness */}
            <div className="pt-2.5 border-t border-ctp-surface0 text-[11px] space-y-1 font-mono">
              <div className="flex justify-between text-ctp-subtext0">
                <span>Source Time:</span>
                <span className="text-ctp-subtext1">{stock.market_timestamp.slice(11, 19)}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-ctp-subtext0">Staleness:</span>
                <span
                  className={`px-1.5 py-0.5 rounded font-semibold ${
                    stock.staleness_seconds > 60
                      ? 'bg-ctp-peach/20 text-ctp-peach'
                      : 'bg-ctp-green/20 text-ctp-green'
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
