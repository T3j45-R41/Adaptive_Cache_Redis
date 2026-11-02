from pydantic import BaseModel, Field
from typing import List, Optional, Dict, Any

class PolicyConfigRequest(BaseModel):
    policy: str = Field(..., description="Eviction policy: LRU, LFU, FIFO, RANDOM, ADAPTIVE")
    capacity: Optional[int] = Field(None, description="Cache capacity limit")

class RunExperimentRequest(BaseModel):
    experiment_name: str = "Real Stock Quote Benchmark"
    workload_type: str = "zipfian"
    cache_capacity: int = 500
    total_requests: int = 1000
    policies: Optional[List[str]] = None
    seed: int = 42

class StockQuoteResponse(BaseModel):
    symbol: str
    price: float
    open_price: float
    high_price: float
    low_price: float
    volume: int
    market_timestamp: str
    is_cached: bool = False
    cached_timestamp: Optional[str] = None
    staleness_seconds: float = 0.0

class OverviewStats(BaseModel):
    active_policy: str
    cache_capacity: int
    current_keys: int
    utilization_pct: float
    total_requests: int
    total_hits: int
    total_misses: int
    hit_rate: float
    miss_rate: float
    average_latency: float
    throughput: float
    total_evictions: int
    expert_lru_weight: Optional[float] = None
    expert_lfu_weight: Optional[float] = None
    active_expert: Optional[str] = None
