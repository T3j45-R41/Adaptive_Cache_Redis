---
tags: [moc, project/adaptive-cache-redis]
status: index
---

# Adaptive Cache Redis — Concept Vault

A study guide mapping every **Operating Systems** and **DBMS** concept the project
actually uses to the code that implements it, plus an honest account of what it
does *not* implement.

> [!info] Project
> **Adaptive Cache Eviction for Real-Time Stock Quote Serving** — Team 075, Code Crackers.
> A Redis-compatible server in C++ whose in-memory store is treated as a **buffer
> pool**, with five interchangeable replacement policies compared under a
> shifting stock-quote workload.

---

## How to use this vault

Read **[[Concept-to-Code Map]]** first — it is the single page that answers
"which concept is where in the code".

Every concept note follows the same shape:

| Section | Meaning |
| --- | --- |
| Definition | The textbook concept, in general terms |
| In this project | What this codebase actually does about it, with `file:line` |
| Callouts | `success` = implemented · `warning` = **not** implemented · `example` = code |

> [!warning] Read the status callouts
> This is an academic project, so the difference between "implemented" and
> "discussed in the docs" matters. Nothing in this vault claims the project
> implements something it does not. See [[Gaps and Future Work]] for the full
> scope boundary.

---

## Maps of Content

### Operating Systems
[[OS Index|→ OS Index]] — 10 notes
Threads and processes · sockets · signals · mutexes · atomics · virtual memory ·
file systems · clocks · system calls · stream framing

### Database Management Systems
[[DBMS Index|→ DBMS Index]] — 10 notes
Buffer pool · replacement policies · adaptive replacement · file organization ·
indexing · query processing · transactions · concurrency control · recovery ·
normalization

### The Project
[[Project Index|→ Project Index]] — 7 notes
Architecture · eviction engine · persistence format · RESP protocol ·
benchmarking · concept map · gaps

---

## The one-paragraph version

The server keeps its entire dataset in RAM inside a single `unordered_map`
(`RedisDatabase::store`). When that map exceeds a configured key limit, an
`EvictionEngine` asks the active replacement policy to nominate a victim and the
key is discarded. **That is a buffer pool with page replacement**, which is the
central storage-management idea of every DBMS textbook — here applied to a cache
of network-served objects rather than to disk pages. Each stored key has a
`CacheEntry` frame recording when it arrived, when it was last read, how often,
how many bytes, and whether it is dirty. The `ADAPTIVE` policy maintains LRU and
LFU experts in lockstep and follows whichever one is currently performing
better, which is the research direction the project is studying.

---

## Status legend

> [!success] Implemented
> Present in the code, tested, and working.

> [!warning] Not implemented
> Discussed for comparison or as an extension. **No code exists for this.**
> Claiming otherwise in a viva would be dishonest.

> [!abstract] Partial
> A simplified form exists; the note says exactly how it differs.

---

## Quick reference

| If you are asked… | Go to |
| --- | --- |
| "Explain your cache architecture" | [[Project/Architecture]] → [[DBMS/Buffer Pool]] |
| "Why did you pick this eviction policy?" | [[DBMS/Buffer Replacement Policies]] |
| "Is your adaptive policy CacheUS?" | [[DBMS/Adaptive Replacement]] |
| "How does your server handle multiple clients?" | [[OS/Processes and Threads]] |
| "How do you avoid data races?" | [[OS/Mutual Exclusion]] |
| "What happens on Ctrl+C?" | [[OS/Signals and Interrupts]] |
| "How do you persist data?" | [[Project/Persistence Format]] → [[OS/File Systems and VFS]] |
| "How do you parse the protocol?" | [[Project/RESP Protocol]] → [[OS/Stream Framing]] |
| "How did you measure it?" | [[Project/Benchmarking]] |
| "What would you do next?" | [[Project/Gaps and Future Work]] |