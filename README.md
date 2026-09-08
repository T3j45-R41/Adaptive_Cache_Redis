# Adaptive Cache Eviction for Real-Time Stock Quote Serving
 
A Redis-compatible in-memory caching server, built from scratch in C++, used to run a comparative study of classical and adaptive cache eviction policies against real historical stock market data.
 
> **Team 075 — Code Crackers** · Project-Based Learning, 5th Semester
 
---
 
## What this is
 
Most cache eviction policies (LRU, LFU, FIFO) use a fixed rule for what to discard when memory fills up.
This project asks: when the "hot" data being cached shifts suddenly — like ticker popularity during an earnings event — does an **adaptive** eviction policy actually outperform the static ones?
 
We built a Redis-compatible server (RESP protocol, raw TCP sockets, pluggable eviction engine) from scratch in C++, then replayed real historical stock price data through it to compare four classical eviction policies against a CACHEUS-style adaptive policy.
