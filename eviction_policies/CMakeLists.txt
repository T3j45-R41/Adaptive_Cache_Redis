cmake_minimum_required(VERSION 3.16)
project(phase2_eviction_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_library(eviction
    src/lru_policy.cpp
    src/lfu_policy.cpp
    src/fifo_policy.cpp
    src/random_policy.cpp
    src/adaptive_policy.cpp
)

add_executable(test_eviction tests/test_eviction.cpp)
target_link_libraries(test_eviction PRIVATE eviction)
