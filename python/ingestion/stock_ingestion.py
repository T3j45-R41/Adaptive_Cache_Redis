import os
import sys
import json
import time
import datetime
import numpy as np
import pandas as pd
from typing import List, Dict, Any, Optional

try:
    import yfinance as yf
except ImportError:
    yf = None

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "database"))
from connection import db_manager

DEFAULT_TICKERS = [
    "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA",
    "NVDA", "META", "INFY", "TCS.NS", "RELIANCE.NS"
]

class StockDataIngestor:
    """
    Ingests historical market quotes from yfinance into MySQL / relational storage.
    Includes realistic synthetic stochastic fallback (Geometric Brownian Motion)
    if offline or API rate-limited.
    """
    def __init__(self, tickers: Optional[List[str]] = None):
        self.tickers = tickers or DEFAULT_TICKERS

    def fetch_real_quotes(self, period="5d", interval="1m") -> List[Dict[str, Any]]:
        quotes = []
        if yf is None:
            return quotes

        for symbol in self.tickers:
            try:
                print(f"[Ingestion] Fetching live/historical data for {symbol}...")
                ticker = yf.Ticker(symbol)
                df = ticker.history(period=period, interval=interval)
                if df is not None and not df.empty:
                    clean_sym = symbol.replace(".NS", "")
                    for ts, row in df.iterrows():
                        market_time = ts.strftime("%Y-%m-%d %H:%M:%S") if hasattr(ts, "strftime") else str(ts)[:19]
                        quotes.append({
                            "symbol": clean_sym,
                            "price": round(float(row["Close"]), 4),
                            "open_price": round(float(row["Open"]), 4),
                            "high_price": round(float(row["High"]), 4),
                            "low_price": round(float(row["Low"]), 4),
                            "volume": int(row["Volume"]),
                            "market_timestamp": market_time
                        })
            except Exception as e:
                print(f"[Ingestion] Warning: Could not fetch {symbol} via yfinance ({e}).")
        return quotes

    def generate_synthetic_quotes(self, count_per_ticker: int = 120) -> List[Dict[str, Any]]:
        """
        Geometric Brownian Motion realistic quote generation.
        dS_t = mu * S_t * dt + sigma * S_t * dW_t
        """
        quotes = []
        base_prices = {
            "AAPL": 225.50, "MSFT": 430.20, "GOOGL": 178.40, "AMZN": 185.30, "TSLA": 250.80,
            "NVDA": 135.60, "META": 580.10, "INFY": 22.80, "TCS": 48.50, "RELIANCE": 35.20
        }
        volatilities = {
            "AAPL": 0.015, "MSFT": 0.012, "GOOGL": 0.018, "AMZN": 0.020, "TSLA": 0.045,
            "NVDA": 0.038, "META": 0.025, "INFY": 0.015, "TCS": 0.012, "RELIANCE": 0.016
        }

        now = datetime.datetime.now()

        for symbol in self.tickers:
            clean_sym = symbol.replace(".NS", "")
            base = base_prices.get(clean_sym, 100.0)
            vol = volatilities.get(clean_sym, 0.02)
            current_price = base

            for i in range(count_per_ticker):
                # 1 minute steps backwards in time
                timestamp = (now - datetime.timedelta(minutes=(count_per_ticker - i))).strftime("%Y-%m-%d %H:%M:%S")
                # Random shock
                ret = np.random.normal(0.0001, vol / np.sqrt(390))
                open_p = current_price
                current_price = max(1.0, current_price * (1.0 + ret))
                high_p = max(open_p, current_price) * (1.0 + abs(np.random.normal(0, 0.002)))
                low_p = min(open_p, current_price) * (1.0 - abs(np.random.normal(0, 0.002)))
                vol_base = int(np.random.lognormal(11.5, 0.8))

                quotes.append({
                    "symbol": clean_sym,
                    "price": round(float(current_price), 4),
                    "open_price": round(float(open_p), 4),
                    "high_price": round(float(high_p), 4),
                    "low_price": round(float(low_p), 4),
                    "volume": vol_base,
                    "market_timestamp": timestamp
                })

        return quotes

    def ingest_into_database(self, quotes: List[Dict[str, Any]]) -> int:
        print(f"[Ingestion] Persisting {len(quotes)} quote records into relational database...")
        query = """
        INSERT INTO stock_data (symbol, price, open_price, high_price, low_price, volume, market_timestamp)
        VALUES (%s, %s, %s, %s, %s, %s, %s)
        """
        inserted = 0
        batch_size = 200
        for i in range(0, len(quotes), batch_size):
            batch = quotes[i:i + batch_size]
            for q in batch:
                try:
                    db_manager.execute_update(query, (
                        q["symbol"], q["price"], q["open_price"], q["high_price"],
                        q["low_price"], q["volume"], q["market_timestamp"]
                    ))
                    inserted += 1
                except Exception as e:
                    pass
        print(f"[Ingestion] Ingestion complete. {inserted} records stored.")
        return inserted

    def run(self, force_synthetic=False):
        quotes = []
        if not force_synthetic:
            try:
                quotes = self.fetch_real_quotes(period="1d", interval="5m")
            except Exception:
                quotes = []

        if not quotes:
            print("[Ingestion] Fetching online data yielded no results; generating synthetic GBM market data.")
            quotes = self.generate_synthetic_quotes(count_per_ticker=60)

        return self.ingest_into_database(quotes)

if __name__ == "__main__":
    ingestor = StockDataIngestor()
    ingestor.run()
