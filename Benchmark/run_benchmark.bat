@echo off
REM Run after starting the custom server in a separate terminal.
python benchmark_client.py --host 127.0.0.1 --port 6379 --operations 200 --concurrency 1 5 10 --label baseline
pause
