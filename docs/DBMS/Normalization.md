---
tags: [dbms, normalization, functional-dependencies, schema-design, project/adaptive-cache-redis]
status: documented-only
---

# Normalization

## Definition

**Normalization** decomposes a relation to reduce redundancy and anomalies
(update, insertion, deletion) while preserving lossless join dependency.

A **functional dependency** `X → Y` means each `X` determines exactly one `Y`.

| Normal form | Requirement |
| --- | --- |
| **1NF** | Atomic values; no repeating groups |
| **2NF** | 1NF + no partial dependency on a proper subset of a composite key |
| **3NF** | 2NF + no transitive dependency of a non-key attribute on a key |
| **BCNF** | Every determinant is a superkey |

The formal test for 3NF: for every nontrivial `X → Y`, `X` must be a superkey,
or every attribute of `Y` must be prime.

---

## In this project

> [!warning] No schema exists
> The server stores **untyped key-value pairs**. There are no relations, no
> attributes, and no declared keys, so normalisation cannot be applied to the
> store itself. This note covers the *workload's* logical schema.

### The workload schema

The stock demo writes keys derived from this record:

```json
{"ticker": "AAPL", "date": "2026-01-04", "close": "181.20", "volume": "54321000"}
```

`ui_app.py` → `cache_history()`
| Key | Contents |
| --- | --- |
| `yf:<TICKER>:close:<YYYY-MM-DD>` | one JSON object per trading day |
| `yf:<TICKER>:latest` | pointer to the most recent daily record |

---

## A denormalised relation and its normalisation

> [!example] Starting point (deliberately over-stuffed, as raw CSV is)
>
> **RAW_QUOTE**
> | ticker | name | sector | date | close | volume | market_cap | currency |
> |---|---|---|---|---|---|---|---|
> | AAPL | Apple Inc. | Technology | 2026-01-02 | 181.20 | 54.3M | 2.7T | USD |
> | AAPL | Apple Inc. | Technology | 2026-01-03 | 183.50 | 52.1M | 2.7T | USD |
> | MSFT | Microsoft | Technology | 2026-01-02 | 411.80 | 21.4M | 3.0T | USD |

**Functional dependencies**

```
R1.  (ticker, date)          → close, volume          [composite candidate key]
R2.  ticker                  → name, sector           [transitive]
R3.  ticker                  → market_cap, currency   [transitive]
R4.  (ticker, currency)      → market_cap             [multivalued dependency]
```

### Not in 3NF

- **R3** is a transitive dependency: `(ticker, date) → ticker → market_cap`.
  Company facts change far less often than prices, so `market_cap` is stored
  redundantly on every row.
- **R4** requires a **multivalued dependency**, which 3NF does not address —
  it needs 4NF.

**Anomalies this causes**
- *Update*: a market-capitalisation correction must rewrite every historical row.
- *Insertion*: a new ticker cannot be recorded until it has a price on that date.
- *Deletion*: removing the only quote for a company erases its sector.

### Decomposition

```
TICKER (ticker PK, name, sector, currency)
MARKET (ticker FK, currency FK, as_of, market_cap)   PK(ticker, currency)
QUOTE   (ticker FK, date, close, volume)              PK(ticker, date)
```

**Check each result**

| Relation | FDs | Normal form |
| --- | --- | --- |
| `TICKER` | `ticker → name, sector, currency` | **3NF** — determinant is the key |
| `MARKET` | `(ticker, currency) → market_cap` | **3NF** |
| `QUOTE` | `(ticker, date) → close, volume` | **3NF**; 2NF holds vacuously — the composite key has no proper subset determining a non-prime attribute |

Lossless: every join is on a foreign key that is a primary key of the target.
Dependency-preserving: every FD survives in a single relation.

### Where the project already does this

> [!info] The key scheme is the decomposition
> The flat CSV row becomes three addressable units:
> - company facts → the ticker itself, constant across dates
> - daily price/volume → `yf:AAPL:close:2026-01-04`
> - the "most recent" pointer → `yf:AAPL:latest`, a *separate* key rather than a
>   duplicated copy of the row
>
> `yf:AAPL:latest` holds only `{ticker, date, close, volume}` — not the company
> facts. Re-pointing it is one write instead of rewriting every historical row.
> That is the update anomaly avoided.

---

## What is missing

> [!warning] Not implemented
> - **No schema definition** of any kind — nothing to normalise in the server.
> - **No constraints.** Nothing enforces that a `ticker` referenced by a quote
>   exists, or that `date` is well-formed.
> - **No 4NF/BFNF.** `MARKET` above needs 4NF because of the multivalued
>   dependency.
> - **No referential integrity on reload.** `load()` restores records verbatim;
>   a dangling pointer key is accepted.
>
> This note is a **design document**, not an implementation.

---

## Related

- [[DBMS/Indexing]] — foreign keys are natural index candidates
- [[DBMS/Query Processing]] — normalised schemas give the optimiser statistics
- [[Project/Benchmarking]] — the CSV generator that motivated this schema
- [[Project/Gaps and Future Work]] — declaring and enforcing the schema