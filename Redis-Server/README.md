# C++ Redis Server with Pluggable Cache Eviction

A lightweight Redis-compatible in-memory data store written in C++, built as the
buffer pool for an adaptive cache-eviction study.

---

## Description
**Name:** `my_redis_server`
**Default Port:** `6379`

Supports strings, lists and hashes over a plain TCP socket using the RESP
protocol, with a pluggable replacement policy, a configurable capacity limit,
live cache statistics, and periodic disk snapshots.

### Commands

- **Common:** `PING`, `ECHO`, `FLUSHALL`, `QUIT`
- **Key/Value:** `SET` (with `EX`/`PX`/`NX`/`XX`), `GET`, `KEYS`, `TYPE`,
  `DEL`/`UNLINK`, `EXISTS`, `EXPIRE`, `TTL`, `PERSIST`, `RENAME`
- **List:** `LGET`, `LLEN`, `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LREM`, `LINDEX`, `LSET`
- **Hash:** `HSET`, `HGET`, `HEXISTS`, `HDEL`, `HLEN`, `HKEYS`, `HVALS`, `HGETALL`, `HMSET`
- **Administration:** `INFO`, `CONFIG GET|SET|RESETSTAT`, `DBSIZE`, `MEMORY USAGE`,
  `EVICTION`, `MAXKEYS`

`EVICTION` and `MAXKEYS` are shorthands kept for the Streamlit UI;
`CONFIG SET POLICY` / `CONFIG SET MAXKEYS` do the same thing.

---

## Repository Structure

```
├── include/
│   ├── RedisServer.h
│   ├── RedisDatabase.h
│   ├── RedisCommandHandler.h
│   └── eviction/
│       ├── eviction_policy.h      # abstract policy interface
│       ├── cache_entry.h          # per-key frame descriptor
│       ├── eviction_metrics.h     # counters
│       ├── eviction_engine.h      # policy factory + capacity + metrics
│       ├── lru_policy.h
│       ├── lfu_policy.h
│       ├── fifo_policy.h
│       ├── random_policy.h
│       └── adaptive_policy.h
├── src/                            # mirrors the header layout
├── tests/
│   ├── test_eviction.cpp           # policy unit tests
│   └── test_integration.cpp        # end-to-end, incl. real sockets
├── Concepts,UseCases&Tests.md
├── README_OS_CONCEPTS.md
├── Makefile
└── test_all.sh
```

The policy sources live here and nowhere else. An earlier revision kept a
second, identical copy under `../eviction_policies/`; the two drifted, and only
the copy under `src/` was ever compiled.

---

## Build

Requires a C++17 compiler and GNU Make.

```bash
make                 # builds my_redis_server
make test            # runs both test suites
make test-policy     # policy unit tests only
make test-integration# command/protocol/eviction/persistence tests only
make clean
```

The test binaries need no network access and no `redis-cli`.

---

## Usage

```bash
./my_redis_server                       # port 6379, dump.my_rdb, snapshot every 300s
./my_redis_server 6380                  # different port
./my_redis_server 6380 mydata.rdb 60    # port, snapshot file, interval in seconds
./my_redis_server 6380 mydata.rdb 0     # interval 0 disables periodic snapshots
```

The policy and capacity are **not** command-line flags; set them at runtime:

```bash
redis-cli -p 6379
> CONFIG SET POLICY LRU
OK
> CONFIG SET MAXKEYS 1000
OK
> INFO
```

Connect with `redis-cli` or any RESP client. Both request forms are accepted:
multibulk (`*3\r\n$3\r\nSET\r\n...`) and inline (`SET foo bar\r\n`). Pipelined
commands and commands larger than one TCP segment are handled.

---

## Design & Architecture

### One keyspace, one type per key
`store` is a single `unordered_map<string, Value>`, where `Value` is a tagged
union of a string, a vector, or a field map. An earlier revision used three
parallel maps, which let one key simultaneously hold a string, a list and a
hash — so `KEYS` returned duplicates, `DEL` counted once, and capacity
accounting was meaningless. Operations against the wrong type now return
`-ERR wrongtype ...`, as in Redis.

### Eviction
`EvictionEngine` owns the active policy, the capacity limit, and the counters.
`RedisDatabase` calls into it on every insert/access/remove and asks for a
victim when the key count exceeds the limit. Policies never see values or
capacity — only keys — which is what lets the same objects serve the server,
the unit tests, and offline replay.

When a policy is switched or a snapshot is loaded, the resident key set is
handed to the new policy **in insertion order**, so FIFO does not resume in
hash order.

| Policy | Structure | Victim selection |
| --- | --- | --- |
| `LRU` | `list` + iterator map | O(1), list back |
| `LFU` | frequency map + logical clock | O(n) scan, oldest-wins tiebreak |
| `FIFO` | `deque` + membership set | O(n) scan with tombstone skipping |
| `RANDOM` | vector + membership set | O(1) uniform draw |
| `ADAPTIVE` | LRU and LFU experts in lockstep | whichever expert currently holds the higher weight |

`ADAPTIVE` is CacheUS-*inspired*, not CacheUS: it scores the losing expert in
the shadow and updates expert weights multiplicatively against the lower-loss
expert, with the accumulated shadow loss decayed each round so the weights
cannot freeze. It has no sliding window, no learned cost predictor, no gradient
training, and no ghost cache. `CACHEUS` is accepted as a legacy alias.

### Frame metadata
`frames` holds a `CacheEntry` per key: insertion and last-access logical
timestamps, access count, byte footprint, and a dirty flag. Frames are marked
clean after every successful snapshot, so `write_backs` in `INFO` counts
evictions that actually cost a write.

### Expiration
Lazy, on access: `purgeExpiredLocked()` reaps due keys using
`steady_clock` (monotonic, so it is immune to wall-clock changes). It collects
the expired keys first and removes them afterwards, because removal touches
`expiry_map` and would otherwise invalidate the iterator being walked.

### Persistence
`dump.my_rdb` is a length-prefixed text format, written to a temporary and
renamed so a crash mid-write cannot truncate the last good snapshot. Every
field carries an explicit byte length, so values containing spaces, colons or
newlines survive a round trip, and time-to-live is written out.

```
K <keylen> <key> <vallen> <value>
L <keylen> <key> <count> (<itemlen> <item>)*
H <keylen> <key> <count> (<fieldlen> <field> <vallen> <value>)*
```
Each record is preceded by its TTL in seconds (`-1` for none).

`load()` parses into staging containers and only swaps them in once the whole
file has parsed, so a truncated or foreign file leaves the running data intact
and reports failure instead of silently emptying the database.

### Concurrency
- Thread-per-connection, with each handler owning a per-connection read buffer.
- A single `std::mutex db_mutex` guards the store. This is the dominant
  performance ceiling and a deliberate simplification, not a scalable design.
- Handler threads are detached, so no `std::thread` objects accumulate for the
  lifetime of the process.
- The signal handler only sets an atomic flag and shuts down the listening
  socket — no iostreams, no `exit()` — and the accept loop performs the final
  snapshot once it returns.

---

## Concepts & Use Cases

- [Concepts,UseCases&Tests.md](Concepts,UseCases&Tests.md) — per-command use cases and design rationale.
- [README_OS_CONCEPTS.md](README_OS_CONCEPTS.md) — sockets, threading, signals, clocks, stream framing.

---

## Known limitations
- The global mutex serialises all operations; there is no sharding or
  reader/writer split.
- LFU and FIFO select their victim with a linear scan, so their eviction cost
  grows with key count, unlike LRU's O(1) splice.
- No replication, clustering, pub/sub, scripting, or secondary indexes.
- `KEYS` is a full scan; there is no `SCAN` cursor.
- Persistence is snapshot-based. There is no write-ahead log, so a crash loses
  everything since the last snapshot.