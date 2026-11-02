import os
import sys
import json
import time
import random
import numpy as np
from datetime import datetime
from typing import List, Dict, Any, Optional

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "database"))
from connection import db_manager

WORKLOAD_TYPES = [
    "normal",
    "popularity_spike",
    "flash_event",
    "changing_popularity",
    "hot_cold",
    "sequential",
    "zipfian",
    "bursty",
    "volatility_transition"
]

class WorkloadGenerator:
    """
    Generates realistic stock market quote access traces based on real quote records.
    Simulates various access distributions (Zipfian, Bursty, Spike, Concept Drift).
    """
    def __init__(self, tickers: Optional[List[str]] = None):
        self.tickers = tickers or [
            "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA",
            "NVDA", "META", "INFY", "TCS", "RELIANCE"
        ]
        # Load quotes from database
        self.quotes_by_ticker = self._load_quotes()

    def _load_quotes(self) -> Dict[str, List[Dict[str, Any]]]:
        quotes_map = {}
        query = "SELECT symbol, price, open_price, high_price, low_price, volume, market_timestamp FROM stock_data"
        try:
            rows = db_manager.execute_query(query)
            for r in rows:
                sym = r["symbol"]
                if sym not in quotes_map:
                    quotes_map[sym] = []
                quotes_map[sym].append(r)
        except Exception as e:
            print(f"[Workload] Error loading quotes: {e}")

        # Fallback if DB empty
        if not quotes_map:
            for sym in self.tickers:
                quotes_map[sym] = [{
                    "symbol": sym, "price": 150.0, "open_price": 149.0,
                    "high_price": 152.0, "low_price": 148.0, "volume": 1000000,
                    "market_timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                }]
        return quotes_map

    def generate_trace(self, 
                       workload_type: str = "zipfian", 
                       total_requests: int = 1000,
                       seed: int = 42) -> List[Dict[str, Any]]:
        np.random.seed(seed)
        random.seed(seed)
        symbols = list(self.quotes_by_ticker.keys())
        if not symbols:
            symbols = self.tickers
        n_symbols = len(symbols)

        requests = []

        if workload_type == "normal":
            # Relatively uniform with slight preferences
            weights = np.ones(n_symbols) / n_symbols
            chosen = np.random.choice(symbols, size=total_requests, p=weights)

        elif workload_type == "zipfian":
            # Zipf distribution (alpha = 1.2)
            ranks = np.arange(1, n_symbols + 1)
            weights = 1.0 / (ranks ** 1.2)
            weights /= weights.sum()
            chosen = np.random.choice(symbols, size=total_requests, p=weights)

        elif workload_type == "hot_cold":
            # 80/20 rule: top 20% get 80% traffic
            n_hot = max(1, int(0.2 * n_symbols))
            hot_symbols = symbols[:n_hot]
            cold_symbols = symbols[n_hot:]

            chosen = []
            for _ in range(total_requests):
                if random.random() < 0.8 and hot_symbols:
                    chosen.append(random.choice(hot_symbols))
                else:
                    chosen.append(random.choice(cold_symbols if cold_symbols else symbols))

        elif workload_type == "popularity_spike":
            # First half: normal zipf; second half: 85% spike on 1 ticker (e.g. NVDA)
            spike_ticker = "NVDA" if "NVDA" in symbols else symbols[0]
            chosen = []
            half = total_requests // 2
            for i in range(total_requests):
                if i < half:
                    chosen.append(random.choice(symbols))
                else:
                    if random.random() < 0.85:
                        chosen.append(spike_ticker)
                    else:
                        chosen.append(random.choice(symbols))

        elif workload_type == "flash_event":
            # Bursts of high volatility tech tickers
            tech = [s for s in symbols if s in ["TSLA", "NVDA", "AAPL", "META"]]
            if not tech:
                tech = symbols[:3]
            chosen = []
            for _ in range(total_requests):
                if random.random() < 0.75:
                    chosen.append(random.choice(tech))
                else:
                    chosen.append(random.choice(symbols))

        elif workload_type == "changing_popularity":
            # Concept drift: Phase 1 prefers group A, Phase 2 prefers group B
            grp_a = symbols[:len(symbols)//2]
            grp_b = symbols[len(symbols)//2:]
            chosen = []
            third = total_requests // 3
            for i in range(total_requests):
                if i < third:
                    chosen.append(random.choice(grp_a if random.random() < 0.8 else grp_b))
                elif i < 2 * third:
                    chosen.append(random.choice(symbols))
                else:
                    chosen.append(random.choice(grp_b if random.random() < 0.8 else grp_a))

        elif workload_type == "sequential":
            chosen = [symbols[i % n_symbols] for i in range(total_requests)]

        elif workload_type == "bursty":
            chosen = []
            current = random.choice(symbols)
            run_len = 0
            max_run = random.randint(5, 20)
            for _ in range(total_requests):
                if run_len >= max_run:
                    current = random.choice(symbols)
                    run_len = 0
                    max_run = random.randint(5, 20)
                chosen.append(current)
                run_len += 1

        elif workload_type == "volatility_transition":
            chosen = []
            for i in range(total_requests):
                progress = i / total_requests
                if random.random() > progress:
                    chosen.append(symbols[0]) # stable
                else:
                    chosen.append(symbols[-1]) # volatile

        else:
            chosen = np.random.choice(symbols, size=total_requests)

        for seq, sym in enumerate(chosen):
            quote_list = self.quotes_by_ticker.get(sym, [])
            quote = random.choice(quote_list) if quote_list else {
                "symbol": sym, "price": 100.0, "volume": 1000,
                "market_timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            }

            # 90% GET requests, 10% SET/update requests (realistic cache workload)
            req_type = "SET" if random.random() < 0.10 else "GET"
            requests.append({
                "sequence_num": seq + 1,
                "symbol": sym,
                "request_type": req_type,
                "quote_data": quote,
                "scheduled_delay_ms": 1
            })

        return requests

    def save_workload_experiment(self, name: str, workload_type: str, 
                                 capacity: int, total_requests: int, 
                                 request_rate: float) -> int:
        query = """
        INSERT INTO workload_experiments 
        (experiment_name, workload_type, cache_capacity, total_requests, request_rate, start_time, status)
        VALUES (%s, %s, %s, %s, %s, %s, %s)
        """
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        exp_id = db_manager.execute_update(query, (
            name, workload_type, capacity, total_requests, request_rate, now, "INITIALIZED"
        ))
        return exp_id

if __name__ == "__main__":
    wg = WorkloadGenerator()
    trace = wg.generate_trace("zipfian", 100)
    print(f"[WorkloadGenerator] Generated trace of {len(trace)} requests.")
    print(f"Sample request: {trace[0]}")
