import os
import sys
from fastapi import APIRouter, HTTPException, BackgroundTasks
from models.schemas import RunExperimentRequest
from services.db_service import db_service

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python", "benchmark"))
from benchmarking_engine import BenchmarkingEngine

router = APIRouter(prefix="/api/experiments", tags=["Experiments"])

bench_engine = BenchmarkingEngine(
    host=os.getenv("REDIS_HOST", "127.0.0.1"),
    port=int(os.getenv("REDIS_PORT", "6379"))
)

@router.get("")
def list_experiments():
    return db_service.get_experiments()

@router.post("")
def create_and_run_experiment(req: RunExperimentRequest):
    try:
        res = bench_engine.run_benchmark_suite(
            experiment_name=req.experiment_name,
            workload_type=req.workload_type,
            cache_capacity=req.cache_capacity,
            total_requests=req.total_requests,
            policies=req.policies,
            seed=req.seed
        )
        return res
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/{id}")
def get_experiment_details(id: int):
    exp = db_service.get_experiment_by_id(id)
    if not exp:
        raise HTTPException(status_code=404, detail="Experiment not found")
    results = db_service.get_benchmark_results(id)
    weights = db_service.get_adaptive_weights(id)
    return {
        "experiment": exp,
        "results": results,
        "adaptive_weights": weights
    }

@router.post("/{id}/start")
def start_experiment(id: int):
    return {"message": f"Experiment {id} started"}

@router.post("/{id}/stop")
def stop_experiment(id: int):
    return {"message": f"Experiment {id} stopped safely"}
