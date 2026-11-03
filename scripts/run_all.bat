@echo off
setlocal enabledelayedexpansion

REM Detect and navigate to project root directory
if exist "%~dp0Redis-Server\my_redis_server.exe" (
    cd /d "%~dp0"
) else (
    cd /d "%~dp0\.."
)

REM Ensure PATH includes Node.js and Python
set "PATH=C:\Program Files\nodejs;C:\Tejas_apps\python;C:\Tejas_apps\python\Scripts;%PATH%"

echo =========================================================
echo  Starting Adaptive Cache Redis & MySQL Analytics System
echo =========================================================
echo [Project Root] %CD%
echo.

REM 1. Verify C++ Redis Server Binary
if not exist "Redis-Server\my_redis_server.exe" (
    echo [ERROR] Redis binary not found at Redis-Server\my_redis_server.exe
    pause
    exit /b 1
)

REM 2. Start C++ Redis Server in background window
echo [1/3] Starting C++ Redis Server on port 6379...
start "AdaptiveCache-Redis-Server" /D "%CD%" "Redis-Server\my_redis_server.exe" 6379 ADAPTIVE 500

REM Wait 2 seconds for Redis server to bind socket
timeout /t 2 /nobreak >nul

REM 3. Start Python FastAPI Backend in background window
echo [2/3] Starting FastAPI Backend on port 8000...
start "AdaptiveCache-Backend-API" /D "%CD%" python -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000

REM Wait 2 seconds for FastAPI to initialize
timeout /t 2 /nobreak >nul

REM 4. Summary & Launch Browser
echo [3/3] Starting React Dashboard on port 5173...
echo.
echo =========================================================
echo  All services launched successfully!
echo   - C++ Redis Server: 127.0.0.1:6379 (Active: ADAPTIVE, Cap: 500)
echo   - FastAPI Backend:  http://127.0.0.1:8000 (Swagger: /docs)
echo   - React Dashboard:  http://localhost:5173 (Catppuccin Latte)
echo =========================================================
echo.

REM Automatically open default web browser
start http://localhost:5173

REM 5. Run Vite Frontend Development Server in this window
cd /d "%CD%\frontend"
call npm.cmd run dev

pause
