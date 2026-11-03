#!/usr/bin/env bash
# =============================================================================
# Master Launch Script: Linux / macOS / WSL
# =============================================================================

set -e

echo "========================================================="
echo " Starting Adaptive Cache Redis & MySQL Analytics System"
echo "========================================================="

# 1. Compile C++ Redis Server
echo "[1/4] Compiling C++ Redis Server..."
cd Redis-Server
make
cd ..

# 2. Start C++ Server in Background
echo "[2/4] Starting C++ Redis Server on port 6379..."
./Redis-Server/my_redis_server 6379 ADAPTIVE 500 &
REDIS_PID=$!

# 3. Start FastAPI Backend in Background
echo "[3/4] Starting FastAPI Backend on port 8000..."
python -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000 &
BACKEND_PID=$!

cleanup() {
    echo "Shutting down background processes..."
    kill $REDIS_PID 2>/dev/null || true
    kill $BACKEND_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# 4. Start React Frontend Dashboard
echo "[4/4] Starting React Frontend Dashboard..."
cd frontend
npm run dev
