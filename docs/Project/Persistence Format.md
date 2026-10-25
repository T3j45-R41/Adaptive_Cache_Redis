---
tags: [project/adaptive-cache-redis, persistence, snapshot, format, project/adaptive-cache-redis]
status: implemented
---

# Persistence Format

## The format

`src/RedisDatabase.cpp:807`–`src/RedisDatabase.cpp:816`

```
Length-prefixed, one record per line:

  <ttl> K <keylen> <key> <vallen> <value>
  <ttl> L <keylen> <key> <count> (<itemlen> <item>)*
  <ttl> H <keylen> <key> <count> (<fieldlen> <field> <vallen> <value>)*
```

`<ttl>` is seconds remaining, or `-1` for no expiry. `<keylen>` and friends are
**byte counts**, so the payload that follows is unambiguous.

### Example

```
499 K 5 plain 6 simple
-1 K 6 spaced 17 has spaces inside
-1 K 8 newlines 18 line1
line2
line3
-1 L 7 listkey 3 10 with space 10 with:colon 12 with
newline
-1 H 6 user:1 2 4 note 5 a:b c 4 name 9 Bob Smith
```

Note that records 3 and 5 span physical lines — the length prefix, not the
newline, is what terminates a value.

---

## Why length prefixes

> [!warning] The original format was lossy
> ```
> K user Tejas
> L tasks clean code test clean code test
> H user:1 name:Bob
> ```
> Space-delimited with no escaping and **no TTL written at all**. Any value
> containing a space or a colon was silently corrupted on reload, and every
> expiry was lost.
>
> A stock quote like `181.20 2026-01-04` would not survive a round trip.

`src/RedisDatabase.cpp:833`
```cpp
void writeChunk(std::ostream& os, const std::string& value) {
    os << value.size() << ' ' << value;
}
```

`src/RedisDatabase.cpp:844`
```cpp
bool readChunk(std::istream& is, std::string& out) {
    std::size_t length = 0;
    if (!readLength(is, length)) return false;

    // readLength leaves the stream immediately after the digits, on the single
    // space that writeChunk put there. Consume exactly that one character --
    // skipping more would eat leading spaces that belong to the data.
    char separator = '\0';
    if (!is.get(separator) || separator != ' ') return false;

    out.assign(length, '\0');
    if (length == 0) return true;
    is.read(&out[0], static_cast<std::streamsize>(length));
    return static_cast<std::size_t>(is.gcount()) == length;
}
```

> [!tip] A bug worth being able to talk about
> That comment marks a real defect. `>>` leaves the stream positioned on the
> separator space, so the first implementation read `" user:"` instead of
> `"user:1"` — every key silently gained a leading space, and only one key
> loaded successfully. Consuming exactly one separator — and no more — is the
> fix, because skipping arbitrary whitespace would eat leading spaces that
> belong to the data.
>
> Regression test: `testPersistenceRoundTrip` covers spaces, colons, embedded
> newlines, trailing spaces, empty strings, lists, hashes, and TTL survival.

---

## Writing

`src/RedisDatabase.cpp:862`–`src/RedisDatabase.cpp:927`

1. Lock `db_mutex` and purge expired keys, so the snapshot is never stale.
2. Write to `<filename>.tmp`.
3. `flush()`, verify the stream, close.
4. `remove()` the old file, then `rename()` the temp over it.
5. Clear every frame's dirty bit.

> [!important] Why temp-then-rename
> Writing in place means a crash, full disk, or power loss leaves a truncated
> file, and the next boot loads half a dataset. `rename(2)` is atomic on POSIX,
> so a reader sees either the old complete file or the new one — never a
> partial. See [[OS/File Systems and VFS]].

Clearing dirty bits is what makes `write_backs` meaningful: it counts evictions
of frames modified since the last successful snapshot.

---

## Reading

`src/RedisDatabase.cpp:932`–`src/RedisDatabase.cpp:975`

1. Lock.
2. Parse into **staging** containers.
3. Reject on any malformed record, unknown tag, or implausible count.
4. Require at least one record.
5. `swap()` into the live store.
6. Rebuild frame metadata and hand the resident set to the eviction policy.
7. Enforce capacity.

> [!warning] Two failure modes this fixes
> **Truncation.** The original cleared the live keyspace *before* parsing, so a
> corrupt file left the server with an empty database. Staging means a bad file
> leaves running data untouched and simply reports failure.
>
> **Empty parse.** The file `this is not a snapshot at all` parsed as zero
> records, which was treated as success. `parsedAny` now distinguishes
> "valid but empty" from "not a snapshot".

### Bounds

```cpp
constexpr std::size_t kMaxRecordsPerKey = 10'000'000;
constexpr std::size_t kMaxRecordBytes = 512u * 1024u * 1024u;
```

A damaged or hostile length field cannot drive an enormous allocation.

---

## Lifecycle

| When | Action |
| --- | --- |
| Every `persistIntervalSeconds` (default 300) | Background snapshot |
| `SIGINT` / `SIGTERM` | Accept loop returns → one final snapshot |
| Startup | `load()`, or start empty if it fails |
| `CONFIG SET MAXKEYS` (lowered) | Evict in memory only; snapshot unchanged |

`0` disables periodic snapshots, so the server saves only on shutdown.

```bash
./my_redis_server 6379 dump.my_rdb 300
#             port   file         interval; 0 disables
```

---

## Limitations

> [!warning] Not implemented
> - **No `fsync`.** `flush()` reaches the OS page cache, not the device.
> - **No write-ahead log.** See [[DBMS/Recovery and Logging]].
> - **No compression, no checksums.** A bit-flip corrupts silently.
> - **No incremental save.** Every snapshot rewrites the entire dataset — O(n)
>   regardless of how much changed.
> - **No versioning.** The format has no magic number; a future change could
>   not detect an old file.
> - **Not a real RDB.** Name only — the on-disk layout is unrelated to Redis's.

---

## Related

- [[DBMS/File Organization]] — pages and records in general
- [[OS/File Systems and VFS]] — atomic rename, `fsync`
- [[DBMS/Recovery and Logging]] — what durability still lacks
- [[Project/Architecture]] — where `dump`/`load` are called