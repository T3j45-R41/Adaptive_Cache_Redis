@echo off
echo =========================================================
echo  Starting Adaptive Cache Redis & MySQL Analytics System
echo =========================================================

REM 1. Start C++ Redis Server
echo [1/3] Starting C++ Redis Server on port 6379...
start "AdaptiveCache-Server" Redis-Server\my_redis_server.exe 6379 ADAPTIVE 500

REM 2. Start FastAPI Backend
echo [2/3] Starting FastAPI Backend on port 8000...
start "AdaptiveCache-Backend" python -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000

REM 3. Start Frontend Dashboard
echo [3/3] Starting React Dashboard on port 5173...
cd frontend
npm run dev
