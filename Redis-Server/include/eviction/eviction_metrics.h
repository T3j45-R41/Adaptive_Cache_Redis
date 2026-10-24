#pragma once
#include <cstdint>
#include <string>

// Counters for the cache subsystem.
//
// hits/misses describe request behaviour; reads/writes describe the I/O a
// buffer pool would incur against secondary storage (a read when a key is
// fetched from the backing store, a write when a dirty frame is evicted).
// Those two are the numbers a buffer-management study actually compares.
struct EvictionMetrics {
    std::uint64_t totalAccesses{0};
    std::uint64_t totalHits{0};
    std::uint64_t totalMisses{0};
    std::uint64_t totalKeysInserted{0};

    std::uint64_t evictions{0};
    std::uint64_t writes{0};        // dirty frames written back on eviction
    std::uint64_t cleanEvictions{0};

    std::uint64_t lruSelections{0};
    std::uint64_t lfuSelections{0};
    std::uint64_t fifoSelections{0};
    std::uint64_t randomSelections{0};
    std::uint64_t adaptiveSelections{0};

    std::uint64_t expirations{0};
    std::uint64_t explicitDeletes{0};
    std::uint64_t policyChanges{0};
    std::uint64_t capacityEnforcements{0};

    std::uint64_t hitRatePercent() const {
        const std::uint64_t total = totalHits + totalMisses;
        if (total == 0) return 0;
        return (totalHits * 100ULL) / total;
    }

    void reset() { *this = EvictionMetrics{}; }
};