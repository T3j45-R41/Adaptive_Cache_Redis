from fastapi import APIRouter
from services.db_service import db_service

router = APIRouter(prefix="/api/benchmark", tags=["Benchmarking"])

@router.get("/results")
def get_benchmark_results():
    return db_service.get_benchmark_results()

@router.get("/comparison")
def get_benchmark_comparison_matrix():
    return db_service.get_comparison_view()
