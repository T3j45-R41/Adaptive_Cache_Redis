import json
import datetime
from fastapi import APIRouter
from services.db_service import db_service
from services.redis_service import redis_service

router = APIRouter(prefix="/api/stocks", tags=["Stock Monitoring"])

@router.get("")
def get_stocks_monitoring():
    raw_stocks = db_service.get_latest_stocks()
    results = []

    for s in raw_stocks:
        sym = s["symbol"]
        cached_json = redis_service.get_cached_key(f"stock:{sym}")
        is_cached = cached_json is not None
        cached_data = None
        staleness = 0.0

        if is_cached:
            try:
                cached_data = json.loads(cached_json)
                src_time = datetime.datetime.strptime(s["market_timestamp"], "%Y-%m-%d %H:%M:%S")
                cached_time = datetime.datetime.strptime(cached_data["market_timestamp"], "%Y-%m-%d %H:%M:%S")
                staleness = max(0.0, (src_time - cached_time).total_seconds())
            except Exception:
                pass

        results.append({
            "symbol": sym,
            "source_price": float(s["price"]),
            "open_price": float(s["open_price"]),
            "high_price": float(s["high_price"]),
            "low_price": float(s["low_price"]),
            "volume": int(s["volume"]),
            "market_timestamp": s["market_timestamp"],
            "is_cached": is_cached,
            "cached_price": float(cached_data["price"]) if cached_data else None,
            "cached_timestamp": cached_data.get("market_timestamp") if cached_data else None,
            "staleness_seconds": round(staleness, 2)
        })

    return results
