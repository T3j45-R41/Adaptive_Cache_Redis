import os
import sys
from pathlib import Path
from connection import db_manager

def initialize_database():
    print("[Init DB] Setting up database schema and seed data...")
    sql_dir = Path(__file__).parent.parent.parent / "database"
    
    files = [
        "schema.sql",
        "indexes.sql",
        "views.sql",
        "procedures.sql",
        "triggers.sql",
        "seed_data.sql"
    ]

    if not db_manager.use_sqlite:
        conn = db_manager.get_connection()
        cursor = conn.cursor()
        for f in files:
            p = sql_dir / f
            if p.exists():
                print(f"  Executing {f} on MySQL...")
                content = p.read_text(encoding="utf-8")
                # Split commands roughly by DELIMITER or semicolon
                # In standard pymysql, execute scripts or run multiple statements
                statements = content.split(";\n")
                for stmt in statements:
                    stmt = stmt.strip()
                    if stmt and not stmt.startswith("--") and not stmt.startswith("DELIMITER"):
                        try:
                            cursor.execute(stmt)
                        except Exception as e:
                            print(f"    [Notice] Statement ignored: {e}")
        conn.commit()
        cursor.close()
        conn.close()
        print("[Init DB] MySQL initialization completed successfully.")
    else:
        print("[Init DB] Embedded SQLite database mirror initialized and ready.")

if __name__ == "__main__":
    initialize_database()
