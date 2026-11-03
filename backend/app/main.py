import os
import sys
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

# Add python path
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python", "database"))
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python", "benchmark"))
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python", "workload"))

from routes.dashboard import router as dashboard_router
from routes.cache import router as cache_router
from routes.experiments import router as experiments_router
from routes.benchmark import router as benchmark_router
from routes.analytics import router as analytics_router
from routes.mysql_admin import router as mysql_router
from routes.stocks import router as stocks_router

app = FastAPI(
    title="Adaptive Cache Redis & MySQL Analytics API",
    description="Backend service for Adaptive Cache Eviction benchmarking, monitoring, and DBMS analytics.",
    version="1.0.0"
)

# Enable CORS for Frontend Development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register Routers
app.include_router(dashboard_router)
app.include_router(cache_router)
app.include_router(experiments_router)
app.include_router(benchmark_router)
app.include_router(analytics_router)
app.include_router(mysql_router)
app.include_router(stocks_router)

@app.get("/api/health")
def health_check():
    from services.redis_service import redis_service
    from connection import db_manager
    return {
        "status": "healthy",
        "redis_connected": redis_service.ping(),
        "database_connected": True,
        "database_engine": "MySQL 8.x" if not db_manager.use_sqlite else "Embedded Relational Engine (SQLite Mirror)",
        "version": "1.0.0"
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)
