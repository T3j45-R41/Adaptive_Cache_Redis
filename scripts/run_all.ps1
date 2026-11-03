# =============================================================================
# Master Launch Script: Windows PowerShell
# =============================================================================

if ($PSScriptRoot -and (Test-Path "$PSScriptRoot\..\Redis-Server")) {
    Set-Location "$PSScriptRoot\.."
}

# Ensure PATH includes Node.js and Python
$env:PATH = "C:\Program Files\nodejs;C:\Tejas_apps\python;C:\Tejas_apps\python\Scripts;$env:PATH"

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host " Starting Adaptive Cache Redis & MySQL Analytics System" -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "Working Directory: $(Get-Location)" -ForegroundColor Gray

# 1. Compile C++ Redis Server if binary doesn't exist
if (-not (Test-Path "Redis-Server\my_redis_server.exe")) {
    Write-Host "[1/4] Compiling C++ Redis Server..." -ForegroundColor Yellow
    $env:PATH = "C:\msys64\ucrt64\bin;D:\MinGW\bin;$env:PATH"
    g++ -std=c++17 -Wall -O2 -static -static-libgcc -static-libstdc++ `
        -IRedis-Server/include `
        Redis-Server/src/main.cpp `
        Redis-Server/src/RedisServer.cpp `
        Redis-Server/src/RedisCommandHandler.cpp `
        Redis-Server/src/RedisDatabase.cpp `
        Redis-Server/eviction/LRUEvictionPolicy.cpp `
        Redis-Server/eviction/LFUEvictionPolicy.cpp `
        Redis-Server/eviction/FIFOEvictionPolicy.cpp `
        Redis-Server/eviction/RandomEvictionPolicy.cpp `
        Redis-Server/eviction/AdaptiveEvictionPolicy.cpp `
        -o Redis-Server/my_redis_server.exe -lws2_32
} else {
    Write-Host "[1/4] C++ Server binary verified." -ForegroundColor Green
}

# 2. Start C++ Redis Server in Background
Write-Host "[2/4] Starting C++ Redis Server on port 6379..." -ForegroundColor Yellow
$redisProcess = Start-Process -FilePath "Redis-Server\my_redis_server.exe" -ArgumentList "6379","ADAPTIVE","500" -WorkingDirectory (Get-Location) -PassThru
Start-Sleep -Seconds 2

# 3. Start Python FastAPI Backend in Background
Write-Host "[3/4] Starting FastAPI Backend on port 8000..." -ForegroundColor Yellow
$backendProcess = Start-Process -FilePath "python" -ArgumentList "-m","uvicorn","backend.app.main:app","--host","0.0.0.0","--port","8000" -WorkingDirectory (Get-Location) -PassThru
Start-Sleep -Seconds 2

# 4. Start React Frontend Dashboard
Write-Host "[4/4] Starting React Frontend Dashboard on port 5173..." -ForegroundColor Yellow
Write-Host "Opening Dashboard at http://localhost:5173" -ForegroundColor Green
Start-Process "http://localhost:5173"

try {
    Set-Location "frontend"
    npm.cmd run dev
} finally {
    Write-Host "Shutting down background services..." -ForegroundColor Red
    if ($redisProcess) { Stop-Process -Id $redisProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($backendProcess) { Stop-Process -Id $backendProcess.Id -Force -ErrorAction SilentlyContinue }
    Set-Location ".."
}
