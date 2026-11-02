import pytest
from fastapi.testclient import TestClient
import sys
import os

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "backend"))
from app.main import app

client = TestClient(app)

def test_health_check():
    response = client.get("/api/health")
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "healthy"
    assert "database_engine" in data

def test_dashboard_overview():
    response = client.get("/api/dashboard/overview")
    assert response.status_code == 200
    data = response.json()
    assert "active_policy" in data
    assert "cache_capacity" in data
    assert "hit_rate" in data

def test_cache_policies():
    response = client.get("/api/cache/policies")
    assert response.status_code == 200
    policies = response.json()["policies"]
    policy_names = [p["name"] for p in policies]
    assert "LRU" in policy_names
    assert "LFU" in policy_names
    assert "FIFO" in policy_names
    assert "RANDOM" in policy_names
    assert "ADAPTIVE" in policy_names

def test_mysql_summary():
    response = client.get("/api/mysql/summary")
    assert response.status_code == 200
    data = response.json()
    assert "tables" in data
    assert "stock_data" in data["tables"]
    assert "benchmark_results" in data["tables"]

def test_query_performance():
    response = client.get("/api/mysql/query-performance")
    assert response.status_code == 200
    data = response.json()
    assert "demonstrations" in data
    assert len(data["demonstrations"]) > 0
