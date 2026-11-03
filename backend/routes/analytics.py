from fastapi import APIRouter
from services.db_service import db_service

router = APIRouter(prefix="/api/analytics", tags=["Analytics"])

@router.get("/hit-rate")
def get_hit_rate_analytics():
    rows = db_service.get_comparison_view()
    return [{"workload": r["workload_type"], "policy": r["policy_name"], "hit_rate": r["hit_rate"]} for r in rows]

@router.get("/latency")
def get_latency_analytics():
    rows = db_service.get_comparison_view()
    return [{
        "workload": r["workload_type"], 
        "policy": r["policy_name"], 
        "avg_latency": r["average_latency"],
        "p95_latency": r["p95_latency"]
    } for r in rows]

@router.get("/staleness")
def get_staleness_analytics():
    rows = db_service.get_comparison_view()
    return [{"workload": r["workload_type"], "policy": r["policy_name"], "staleness_sec": r["staleness"]} for r in rows]

@router.get("/adaptive/weights")
def get_adaptive_weights_analytics():
    return db_service.get_adaptive_weights()
