---
tags: [dbms, storage, file-organization, pages, records, project/adaptive-cache-redis]
status: partial
---

# File Organization

## Definition

Below the buffer pool, data lives in files organised as **pages** (blocks), the
unit of I/O. Physical organisation determines how efficiently records are found.

| Method | Layout | Best for |
| --- | --- | --- |
| **Heap (unsorted)** | Records appended in arrival order | Bulk load, full scans |
| **Sorted** | Ordered by a key | Range queries, sequential access |
| **Hashed** | Bucket by hash of key | Exact-match lookups |
| **Clustered** | Ordered by primary key | Index scans |

### Slotted pages

A page cannot hold fixed-length records if values vary in size, so real systems
use a **slotted page**:

```
┌─────────────────────────────────────┐
│ Header: free-space offset, slot ct  │
├─────────────────────────────────────┤
│ Slot 0: offset 1204, length 48      │  ← directory
│ Slot 1: offset 1120, length 84      │
│ ...                                 │
├─────────────────────────────────────┤
│         free space                  │
├─────────────────────────────────────┤
│ Record 1            Record 0        │  ← grow toward the header
└─────────────────────────────────────┘
```

The slot directory stays fixed; records move when they grow. This supports
**variable-length records** and in-place updates with minimal copying.

---

## In this project

### Records on disk

`src/RedisDatabase.cpp:807`–`src/RedisDatabase.cpp:816`
```
Length-prefixed, one record per line:

  K <keylen> <key> <vallen> <value>
  L <keylen> <key> <count> (<itemlen> <item>)*
  H <keylen> <key> <count> (<fieldlen> <field> <vallen> <value>)*
  E <keylen> <key> <ttl-seconds>
```

Each record is preceded by its TTL in seconds (`-1` for none).

> [!warning] This is NOT a slotted page
> It is a flat, line-oriented text format. There are no pages, no slot
> directory, and no free-space management. Compare honestly against the
> diagram above when presenting — say "record-oriented with length prefixes,
> not page-oriented".

### Why length prefixes

`src/RedisDatabase.cpp:833`
```cpp
void writeChunk(std::ostream& os, const std::string& value) {
    os << value.size() << ' ' << value;
}
```

> [!important] The bug this fixed
> The original format was space-delimited with no escaping:
> ```
> K user Tejas
> H user:1 name:Bob
> ```
> Any value containing a **space** or a **colon** was silently corrupted on
> reload. A stock quote like `"181.20 2026-01-04"` would not round-trip.
>
> Length prefixes make arbitrary bytes safe, because the reader knows exactly
> how many bytes to consume.

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
> That comment marks a real defect found during development. `>>` leaves the
> stream on the separator space, so the first naive implementation read
> `" user:"` instead of `"user:1"` — every key gained a leading space. The fix
> consumes exactly one separator, and no more, because skipping arbitrary
> whitespace would eat leading spaces that belong to the data.
>
> Regression test: `testPersistenceRoundTrip` verifies values containing
> spaces, colons, newlines, trailing spaces, and empty strings.

### Corrupt-file defence

`src/RedisDatabase.cpp:830`
```cpp
constexpr std::size_t kMaxRecordsPerKey = 10'000'000;
constexpr std::size_t kMaxRecordBytes = 512u * 1024u * 1024u;
```

A hostile or damaged file cannot drive an enormous allocation.

---

## Not implemented

> [!warning] Absent
> - **Pages.** No fixed-size blocks, no page header, no page id.
> - **Slotted pages and free-space management.** No slot directory, no in-place
>   update, no compaction.
> - **Heap / sorted / hashed file organisation.** Records are only reachable by
>   key lookup; there is no range scan over the file.
> - **Record compression or encoding.** Payloads are stored verbatim.
> - **File segmentation.** One monolithic file.

---

## Related

- [[DBMS/Buffer Pool]] — the memory side of the same story
- [[DBMS/Indexing]] — how records are located
- [[Project/Persistence Format]] — the full format specification
- [[OS/File Systems and VFS]] — the atomic rename that protects the file
- [[Project/Gaps and Future Work]] — slotted pages as an extension