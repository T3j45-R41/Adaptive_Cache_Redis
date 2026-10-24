#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Per-key frame descriptor kept alongside the cached value.
//
// This is the buffer-pool bookkeeping record: it tracks when a key entered the
// pool, when it was last touched, how often it was read, how many bytes it
// occupies, and whether it has been modified since the last write-back.
// The value payload itself lives in RedisDatabase; eviction policies only ever
// need the key and this metadata, so they never depend on the payload type.
struct CacheEntry {
    std::string key;

    // Logical clock ticks, not wall-clock time, so replayed benchmarks and unit
    // tests are deterministic.
    std::uint64_t insertionTime{0};
    std::uint64_t lastAccessTime{0};
    std::uint64_t accessCount{0};

    // Approximate resident footprint: key + payload + per-entry overhead.
    std::size_t byteSize{0};

    // Modified since the last snapshot write-back. An evicted dirty frame costs
    // one write I/O; a clean one costs nothing.
    bool dirty{false};
};