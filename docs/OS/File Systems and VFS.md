---
tags: [os, filesystem, vfs, io, durability, project/adaptive-cache-redis]
status: implemented
---

# File Systems and VFS

## Definition

The **VFS** (Virtual File System) is the kernel layer that gives `open`,
`read`, `write`, and `close` a single interface regardless of the underlying
filesystem. A file descriptor indexes the per-process descriptor table, and
that entry points at an inode (or its Windows equivalent) holding the metadata.

Durability rests on two guarantees:
- **`fsync`/`fdatasync`** — flush the OS buffer to the device.
- **Atomic rename** — `rename(2)` is atomic on POSIX: the destination is
  replaced in a single step, never observed half-written.

---

## In this project

### The snapshot file

`src/RedisDatabase.cpp:862`–`src/RedisDatabase.cpp:927`
```cpp
bool RedisDatabase::dump(const std::string& filename) {
    std::lock_guard<std::mutex> lock(db_mutex);
    purgeExpiredLocked();

    // Write to a temporary and rename, so a crash mid-write cannot truncate
    // the previous good snapshot.
    const std::string temp = filename + ".tmp";
    std::ofstream ofs(temp, std::ios::binary | std::ios::trunc);
    ...
    ofs.flush();
    if (!ofs) { ofs.close(); removeFile(temp); return false; }
    ofs.close();

    removeFile(filename);
    if (!renameFile(temp, filename)) {
        removeFile(temp);
        return false;
    }
```

> [!important] Why temp-then-rename
> Writing directly to `dump.my_rdb` means a crash, a full disk, or a power loss
> mid-write leaves a truncated file. On the next boot the server would load
> **half a dataset** — or fail to parse it at all. Writing to a sibling and
> renaming makes the swap atomic: readers see either the old complete file or
> the new complete file, never a partial one.

### Clean frames after a successful snapshot

`src/RedisDatabase.cpp:925`
```cpp
// Everything on disk now matches memory, so no frame is dirty any more.
for (auto& entry : frames) entry.second.dirty = false;
```

This is what makes `write_backs` in `INFO` meaningful: it counts evictions of
frames modified since the last successful snapshot — the actual I/O cost of
running with a small cache.

### Loading is staged, then committed

`src/RedisDatabase.cpp:932`–`src/RedisDatabase.cpp:975`
```cpp
std::unordered_map<std::string, Value> stagedStore;
std::unordered_map<std::string, Clock::time_point> stagedExpiry;
...
if (!parsedAny) return false;

store.swap(stagedStore);
expiry_map.swap(stagedExpiry);
frames.clear();
```

> [!warning] A bug this prevents
> The original cleared the live keyspace *first* and then parsed. A truncated
> or foreign file therefore left the server with an empty database. Parsing
> into staging containers and swapping only on full success means a bad file
> leaves the running data completely intact.

### Named wrappers for `std::remove`

`src/RedisDatabase.cpp:823`–`src/RedisDatabase.cpp:825`
```cpp
bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
bool renameFile(const std::string& from, const std::string& to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}
```

> [!tip] Why the wrappers exist
> `std::remove` is overloaded: `<algorithm>` gives
> `remove(first, last, value)` and `<cstdio>` gives `remove(const char*)`.
> With both headers included the one-argument form resolves to the file
> version — correct, but alarming to read. Naming them removes the ambiguity.

---

## Limitations

> [!warning] Not implemented
> - **No `fsync`.** `ofs.flush()` pushes to the OS buffer, not to the device.
>   A power loss can still lose the last snapshot.
> - **No AOF / write-ahead log.** See [[DBMS/Recovery and Logging]].
> - **No directory management.** The dump path is used as given; the parent
>   directory is never created.
> - **No locking between processes.** Two servers on the same dump file would
>   corrupt each other; `flock`/`fcntl` locking is absent.

---

## Related

- [[Project/Persistence Format]] — the record format inside the file
- [[DBMS/File Organization]] — pages and records
- [[DBMS/Recovery and Logging]] — what durability really costs
- [[OS/System Calls]] — the syscalls behind `ofstream`