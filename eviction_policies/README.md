# Phase 2 Eviction & Adaptive Policy Starter

This module is a Phase-2 starting implementation for the project:

"Adaptive Cache Eviction for Real-Time Stock Quote Serving"

## Scope

Included:
- common eviction-policy interface
- LRU
- LFU
- FIFO
- Random
- initial adaptive controller
- LRU + LFU as the two adaptive experts
- shadow-style reuse feedback
- regret-like weight update
- unit tests

Not included:
- TCP/RESP server
- yfinance workload generator
- benchmark client
- final performance evaluation
- production concurrency handling
- final CACHEUS reproduction

## Important academic note

The Phase-1 materials specify a CACHEUS-style adaptive strategy using two experts,
shadow evaluation and regret minimization, but they do not specify the exact
expert pair or mathematical update rule.

Therefore this starter implementation makes an explicit Phase-2 design choice:
LRU and LFU are the two experts, and a lightweight regret-based weighted
controller is used. It should be described as a "CACHEUS-inspired/initial
adaptive mechanism", not as an exact reproduction of CACHEUS.

The expert pair and update rule can be revised in Phase 3 after discussion
with the mentor and after workload-based evaluation.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/test_eviction
```

## Integration contract

When the cache receives a request:
- on insert: call `onInsert(key)`
- on successful access: call `onAccess(key)`
- before/when removing: call `onRemove(key)`

When an adaptive eviction is required:
1. call `selectVictim()`
2. call `registerEvictionDecision(victim)`
3. remove victim from the actual cache
4. call `onRemove(victim)`

The surrounding cache server remains responsible for capacity management,
actual storage, networking and request handling.
