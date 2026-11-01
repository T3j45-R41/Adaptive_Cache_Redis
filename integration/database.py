#!/usr/bin/env python3
"""SQLite Database persistence and DBMS integration for Adaptive_Cache_Redis. 

Re-exports DatabaseManager, AdaptiveCacheDB, and helper functions from database_manager.py
for seamless module compatibility.
"""

from __future__ import annotations

from database_manager import (
    DEFAULT_DB_PATH,
    SCHEMA_DDL as SCHEMA_SQL,
    DatabaseManager,
    AdaptiveCacheDB,
    get_db_manager,
)

def get_db(db_path=None) -> DatabaseManager:
    """Helper to obtain an AdaptiveCacheDB / DatabaseManager instance."""
    return DatabaseManager(db_path)

__all__ = [
    "DEFAULT_DB_PATH",
    "SCHEMA_SQL",
    "DatabaseManager",
    "AdaptiveCacheDB",
    "get_db",
    "get_db_manager",
]
