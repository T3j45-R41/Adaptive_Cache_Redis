# MySQL Database Setup Guide: adaptive_cache_db

This guide details how to set up, configure, and initialize the MySQL 8.x database for the **Adaptive Cache Eviction Redis Server** project.

---

## 1. Prerequisites
- **MySQL Server 8.0+** installed.
- Root or administrative privileges to create databases and user grants.
- MySQL client CLI (`mysql`) or MySQL Workbench.

---

## 2. Environment Configuration
The application reads database connection parameters from environment variables (or `.env` file):

```bash
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=your_secure_password
MYSQL_DATABASE=adaptive_cache_db
```

---

## 3. Database Initialization

Execute the SQL scripts in order:

### Option A: Via Command Line (Linux / macOS / Git Bash / PowerShell)

```bash
# 1. Create database and tables
mysql -u root -p < database/schema.sql

# 2. Create performance indexes
mysql -u root -p < database/indexes.sql

# 3. Create views
mysql -u root -p < database/views.sql

# 4. Create stored procedures
mysql -u root -p < database/procedures.sql

# 5. Create triggers
mysql -u root -p < database/triggers.sql

# 6. Insert seed data
mysql -u root -p < database/seed_data.sql
```

### Option B: Master Initialization Script
A single Python helper script is provided in `python/database/init_db.py`:

```bash
python python/database/init_db.py
```

---

## 4. Verification

Run the verification queries:

```sql
USE adaptive_cache_db;
SHOW TABLES;
SELECT * FROM eviction_policies;
SELECT * FROM vw_policy_performance;
```
