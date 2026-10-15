@echo off
if "%~1"=="" (
  echo Usage: run_analysis.bat results\benchmark_YYYYMMDD_HHMMSS.csv
  pause
  exit /b 1
)
python analyze_results.py --input "%~1" --output-dir analysis_output
pause
