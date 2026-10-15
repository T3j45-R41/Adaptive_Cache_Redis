#pragma once
#include <cstdint>
#include <string>

struct EvictionMetrics {
    std::uint64_t evictions{0};
    std::uint64_t lruSelections{0};
    std::uint64_t lfuSelections{0};
    std::uint64_t fifoSelections{0};
    std::uint64_t randomSelections{0};
    std::uint64_t adaptiveSelections{0};
};
