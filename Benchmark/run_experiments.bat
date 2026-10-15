@echo off
REM Ensure the Redis server is running in a separate terminal:
REM   cd ..\Redis-Server
REM   my_redis_server.exe 6379 LRU 25

echo ========================================================
echo Running Eviction Policy Comparison on Stock Market Data
echo ========================================================

echo [1/5] Benchmarking FIFO Policy...
python benchmark_client.py --stock-csv stock_market_data.csv --policy FIFO --label FIFO --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results

echo [2/5] Benchmarking LRU Policy...
python benchmark_client.py --stock-csv stock_market_data.csv --policy LRU --label LRU --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results

echo [3/5] Benchmarking LFU Policy...
python benchmark_client.py --stock-csv stock_market_data.csv --policy LFU --label LFU --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results

echo [4/5] Benchmarking RANDOM Policy...
python benchmark_client.py --stock-csv stock_market_data.csv --policy RANDOM --label RANDOM --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results

echo [5/5] Benchmarking CACHEUS (Adaptive) Policy...
python benchmark_client.py --stock-csv stock_market_data.csv --policy CACHEUS --label CACHEUS --max-keys 25 --operations 300 --concurrency 1 5 --output-dir results

echo ========================================================
echo Generating Comparative Charts and Markdown Report...
echo ========================================================
python analyze_results.py --input results --output-dir analysis_output

echo Complete! Check analysis_output folder for charts and report.
pause
