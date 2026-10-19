---
tags: [dbms, query-processing, optimizer, cost-model, execution-plans, project/adaptive-cache-redis]
status: not-implemented
---

# Query Processing

## Definition

Turning a declarative request into an execution plan involves three stages:

| Stage | Input → Output |
| --- | --- |
| **Parser / analyser** | Text → logical query tree; resolve names against the schema |
| **Logical optimiser** | Rewrite using equivalence rules (push selections down, reorder joins) |
| **Physical optimiser** | Choose access paths and join algorithms using a **cost model** |

The cost model estimates I/O, CPU, and memory from statistics. A classic
selectivity estimate for equality:

```
sel(p) = 1 / V(p)          where V(p) = number of distinct values of p
```

and for range predicates, using a histogram or uniform assumption:

```
sel(p < c) = (c - p_min) / (p_max - p_min)
```

Plan comparison then multiplies selectivity by cardinality down the tree.

---

## In this project

> [!warning] Not implemented
> There is no query layer. Commands are dispatched by name through a chain of
> `if` comparisons, not parsed into a tree:
>
> ```cpp
> if (cmd == "SET")      return handleSet(tokens, db);
> if (cmd == "GET")      return handleGet(tokens, db);
> if (cmd == "KEYS")     return handleKeys(tokens, db);
> ...
> ```
>
> There is no SELECT statement, no predicate, no join, no optimiser, and no
> statistics catalogue.

### The one place a cost decision exists

> [!important] The buffer manager *is* an optimiser
> Choosing which page to evict is a cost-based decision: keep the data whose
> future reuse cost is lowest. This project implements that decision with a
> learning policy instead of a formula.
>
> In that narrow sense, [[DBMS/Adaptive Replacement]] is an online cost model
> for one specific decision. That is a genuine, if small, piece of query-cost
> reasoning — and it is worth stating precisely rather than overclaiming.

The statistic available to any such decision is the hit rate:

`include/eviction/eviction_metrics.h`
```cpp
std::uint64_t hitRatePercent() const {
    const std::uint64_t total = totalHits + totalMisses;
    if (total == 0) return 0;
    return (totalHits * 100ULL) / total;
}
```

> [!abstract] Crude, and it should be called crude
> This is **integer division**, so 2 hits in 3 lookups reports 66, not 67.
> There is no histogram, no selectivity estimate, no reordering — it is a
> single aggregate figure. Do not present it as a cost model.

---

## What a minimal version would look like

> [!tip] Scoped extension
> The project already has a workload whose natural queries are range-shaped —
> the stock data uses `yf:<TICKER>:close:<YYYY-MM-DD>`, i.e. a composite key with
> meaningful ordering. A minimal query layer would be:
>
> 1. `RANGE <key-prefix> [start] [end]` — a range scan over one access path.
> 2. `EXPLAIN RANGE ...` — report which access path was chosen and why.
> 3. A choice between two access paths: hash point lookup vs. ordered scan.
>
> Step 3 is what makes it a query-optimisation exercise rather than a feature:
> the planner must choose using estimated cost, and the `EXPLAIN` output is the
> evidence.

---

## Related

- [[DBMS/Indexing]] — the access paths an optimiser chooses between
- [[DBMS/Adaptive Replacement]] — the one online cost decision that exists
- [[DBMS/Normalization]] — schema design, upstream of the optimiser
- [[DBMS/Buffer Replacement Policies]] — selection cost vs. cache quality
- [[Project/Gaps and Future Work]] — the scoped extension above