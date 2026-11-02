import pytest
import sys
import os

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "python", "workload"))
from workload_generator import WorkloadGenerator, WORKLOAD_TYPES

def test_workload_generator_initialization():
    wg = WorkloadGenerator()
    assert len(wg.tickers) > 0

def test_all_workload_distributions():
    wg = WorkloadGenerator()
    for w_type in WORKLOAD_TYPES:
        trace = wg.generate_trace(workload_type=w_type, total_requests=100, seed=123)
        assert len(trace) == 100
        assert trace[0]["sequence_num"] == 1
        assert trace[0]["symbol"] in wg.tickers
        assert trace[0]["request_type"] in ["GET", "SET"]
