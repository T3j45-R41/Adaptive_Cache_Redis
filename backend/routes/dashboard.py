from fastapi import APIRouter
from services.redis_service import redis_service
from services.db_service import db_service

router = APIRouter(prefix="/api/dashboard", tags=["Dashboard"])

@router.get("/overview")
def get_dashboard_overview():
    # 1. Fetch live metrics from C++ cache server
    redis_info = redis_service.get_info()
    evict_stats = redis_service.get_eviction_stats()

    active_policy = evict_stats.get("policy", redis_info.get("policy_name", "ADAPTIVE"))
    capacity = int(evict_stats.get("capacity", redis_info.get("maxmemory_keys", 1000)))
    current_keys = int(evict_stats.get("current_size", redis_info.get("used_memory_keys", 0)))
    total_hits = int(evict_stats.get("hits", redis_info.get("keyspace_hits", 0)))
    total_misses = int(evict_stats.get("misses", redis_info.get("keyspace_misses", 0)))
    total_evictions = int(evict_stats.get("evictions", redis_info.get("evicted_keys", 0)))

    total_requests = total_hits + total_misses
    hit_rate = round((total_hits / total_requests * 100.0), 2) if total_requests > 0 else 0.0
    miss_rate = round(100.0 - hit_rate, 2)
    utilization_pct = round((current_keys / capacity * 100.0), 2) if capacity > 0 else 0.0

    # 2. Historical comparison summary from DB
    perf_views = db_service.get_policy_performance_view()
    comparison_views = db_service.get_comparison_view()

    return {
        "status": "healthy" if redis_service.ping() else "cache_offline",
        "active_policy": active_policy,
        "cache_capacity": capacity,
        "current_keys": current_keys,
        "utilization_pct": utilization_pct,
        "total_requests": total_requests,
        "total_hits": total_hits,
        "total_misses": total_misses,
        "hit_rate": hit_rate,
        "miss_rate": miss_rate,
        "total_evictions": total_evictions,
        "average_latency": float(comparison_views[0]["average_latency"]) if comparison_views else 0.05,
        "throughput": float(comparison_views[0]["throughput"]) if comparison_views else 12500.0,
        "expert_lru_weight": float(evict_stats.get("expert1_weight", 0.5)),
        "expert_lfu_weight": float(evict_stats.get("expert2_weight", 0.5)),
        "cumulative_regret1": float(evict_stats.get("cumulative_regret1", 0.0)),
        "cumulative_regret2": float(evict_stats.get("cumulative_regret2", 0.0)),
        "active_expert": evict_stats.get("active_expert", "LRU"),
        "recent_benchmarks": comparison_views[:10],
        "policy_performance": perf_views
    }
