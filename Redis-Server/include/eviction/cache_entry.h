#pragma once
#include <cstdint>
#include <string>

struct CacheEntry {
    std::string key;
    std::string value;
    std::uint64_t insertionTime{0};
    std::uint64_t lastAccessTime{0};
    std::uint64_t accessCount{0};
};
