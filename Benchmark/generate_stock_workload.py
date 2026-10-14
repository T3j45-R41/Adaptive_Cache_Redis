import csv
import random
from datetime import datetime, timedelta

def generate_stock_data(filepath="stock_market_data.csv", num_rows=1000):
    tickers = [
        "AAPL", "MSFT", "NVDA", "AMZN", "GOOGL", "META", "TSLA", "BRK.B", "UNH", "JNJ",
        "JPM", "V", "PG", "XOM", "HD", "MA", "CVX", "MRK", "ABBV", "PEP",
        "KO", "COST", "AVGO", "TMO", "CSCO", "MCD", "WMT", "ADBE", "CRM", "BAC"
    ]
    
    start_time = datetime(2026, 10, 1, 9, 30, 0)
    prices = {t: round(random.uniform(50.0, 500.0), 2) for t in tickers}
    
    rows = []
    current_time = start_time
    
    # Simulate realistic stock market session with shifting hot tickers (news events / earnings)
    for i in range(num_rows):
        current_time += timedelta(seconds=random.randint(5, 30))
        
        # Phases simulate market dynamics:
        # Phase 1 (0..300): Tech giants heavy (AAPL, NVDA, MSFT)
        # Phase 2 (300..700): Sudden earnings break out in healthcare & energy (MRK, CVX, XOM)
        # Phase 3 (700..1000): Broad market scanning
        if i < 300:
            weights = [10 if t in ("AAPL", "NVDA", "MSFT", "TSLA") else 1 for t in tickers]
        elif i < 700:
            weights = [15 if t in ("MRK", "CVX", "XOM", "JPM", "BAC") else 1 for t in tickers]
        else:
            weights = [1 for _ in tickers]
            
        ticker = random.choices(tickers, weights=weights, k=1)[0]
        # Random walk for price
        prices[ticker] = round(max(5.0, prices[ticker] + random.uniform(-0.5, 0.5)), 2)
        
        rows.append({
            "Datetime": current_time.isoformat(),
            "Ticker": ticker,
            "Close": f"{prices[ticker]:.2f}"
        })
        
    with open(filepath, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["Datetime", "Ticker", "Close"])
        writer.writeheader()
        writer.writerows(rows)
        
    print(f"Generated {num_rows} stock records in {filepath}")

if __name__ == "__main__":
    generate_stock_data()
