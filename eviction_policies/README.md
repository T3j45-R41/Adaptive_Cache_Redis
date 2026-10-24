# Eviction Policies

The replacement policies for the cache, and the engine that drives them.

## Where the code lives

Everything is under `../Redis-Server`:

```
headers  ->  ../Redis-Server/include/eviction/
sources  ->  ../Redis-Server/src/eviction/
tests    ->  ../Redis-Server/tests/test_eviction.cpp
```

This directory used to hold a second, byte-identical copy of every policy file.
Two copies of the same code drift — and these did: only the copy under
`src/` was ever compiled by the Makefile, so the duplicate was dead weight that
looked like the real implementation. The CMakeLists here is kept only for
anyone already using CMake; it builds from the canonical sources.

## Policies

| Policy | Data structure | Notes |
| --- | --- | --- |
| `LRU` | `std::list` + key→iterator map | O(1) splice and O(1) victim |
| `LFU` | frequency map + logical clock | O(n) victim scan, recency tiebreak |
| `FIFO` | `std::deque` + membership set | Ignores reads entirely |
| `RANDOM` | vector + membership set | Null model for the comparison |
| `ADAPTIVE` | LRU and LFU experts in lockstep | Weighted choice between the two |

`CACHEUS` is accepted as a legacy alias for `ADAPTIVE`.

## The policy contract

A policy is told which keys are resident and in what order they are touched,
and nominates one victim when the owner needs space. It never sees values,
capacity, or a clock — which is what lets the same objects serve the server,
the unit tests, and offline workload replay.

```cpp
onInsert(key)                // a key arrived
onAccess(key)                // a key was read
onRemove(key)                // a key left for any reason
onReplace(key)               // value rewritten: default no-op, must not reset recency/frequency
selectVictim()               // nominate a key, or "" if none
registerEvictionDecision(v)  // the owner acted; stateless policies ignore this
syncKeys(keys)               // bulk reconcile after a switch or a snapshot load
```

Eviction sequence, in order:

1. `selectVictim()` to nominate
2. `registerEvictionDecision(victim)` — **before** removal, while both experts
   still agree on the resident set and can each leave a shadow candidate
3. remove the key from the store
4. `onRemove(victim)`

## About the adaptive controller

`ADAPTIVE` maintains an LRU expert and an LFU expert in lockstep over the same
resident keys, so exactly one key is ever really evicted. Before each decision
each expert nominates its own candidate; the engine takes one and leaves the
other as a shadow. If a shadow candidate is requested again shortly afterwards,
that expert's ordering was wrong, and the weights are updated multiplicatively
against the lower-loss expert. Accumulated loss is decayed each round so
recent evidence dominates and the weights cannot freeze at their initial
values; a floor of 0.05 stops either expert from being driven to zero, which
would leave it unable to recover after a workload shift.

This is CacheUS-*inspired*, not CacheUS. It shares the idea of scoring
alternatives in the shadow, but it has **no sliding access window, no learned
cost predictor, no gradient training, and no ghost cache**. Report it as
ADAPTIVE. If a faithful CacheUS reproduction is required, the missing pieces
are the ones above, not a bug in what is here.

## Build and test

```bash
cd ../Redis-Server
make test-policy
```

`../eviction_policies` offers the same tests through CMake:

```bash
cmake -S . -B build && cmake --build build && ./build/test_eviction
```