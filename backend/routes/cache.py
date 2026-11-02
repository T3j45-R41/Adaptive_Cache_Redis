from fastapi import APIRouter, HTTPException
from models.schemas import PolicyConfigRequest
from services.redis_service import redis_service

router = APIRouter(prefix="/api/cache", tags=["Cache Management"])

@router.get("/stats")
def get_cache_stats():
    info = redis_service.get_info()
    evict = redis_service.get_eviction_stats()
    return {
        "server_info": info,
        "eviction_stats": evict,
        "is_online": redis_service.ping()
    }

@router.get("/policies")
def get_available_policies():
    return {
        "policies": [
            {"name": "LRU", "description": "Least Recently Used (Recency)", "is_adaptive": False},
            {"name": "LFU", "description": "Least Frequently Used (Frequency)", "is_adaptive": False},
            {"name": "FIFO", "description": "First-In First-Out (Chronological)", "is_adaptive": False},
            {"name": "RANDOM", "description": "Random Replacement (Uniform)", "is_adaptive": False},
            {"name": "ADAPTIVE", "description": "Adaptive CACHEUS Regret Minimization", "is_adaptive": True}
        ]
    }

@router.post("/policy")
def set_cache_policy(req: PolicyConfigRequest):
    p = req.policy.upper()
    if p not in ["LRU", "LFU", "FIFO", "RANDOM", "ADAPTIVE"]:
        raise HTTPException(status_code=400, detail=f"Invalid policy '{req.policy}'")

    success = redis_service.set_policy(p)
    if req.capacity and req.capacity > 0:
        redis_service.set_capacity(req.capacity)

    return {
        "success": success,
        "active_policy": p,
        "capacity": req.capacity
    }
