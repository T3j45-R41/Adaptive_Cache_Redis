# Optional CMake build for the eviction policy library and its unit tests.
#
# The canonical sources live with the server:
#   headers  -> ../Redis-Server/include/eviction
#   sources  -> ../Redis-Server/src/eviction
#   tests    -> ../Redis-Server/tests/test_eviction.cpp
#
# These files used to be duplicated in this directory, which meant every policy
# change had to be applied twice and the two copies silently drifted. `make -C
# ../Redis-Server test-policy` builds the same target and is the normal path;
# this file exists only for anyone already using CMake.

cmake_minimum_required(VERSION 3.16)
project(adaptive_cache_eviction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SERVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Redis-Server)

file(GLOB EVICTION_SOURCES ${SERVER_DIR}/src/eviction/*.cpp)

# Library: the policies on their own, with no dependency on the server.
add_library(eviction STATIC ${EVICTION_SOURCES})
target_include_directories(eviction PUBLIC ${SERVER_DIR}/include/eviction)
find_package(Threads REQUIRED)
target_link_libraries(eviction PUBLIC Threads::Threads)

# Unit tests.
add_executable(test_eviction ${SERVER_DIR}/tests/test_eviction.cpp)
target_link_libraries(test_eviction PRIVATE eviction)

enable_testing()
add_test(NAME eviction_unit_tests COMMAND test_eviction)

# The full server, including the eviction engine.
add_executable(my_redis_server
    ${SERVER_DIR}/src/main.cpp
    ${SERVER_DIR}/src/RedisServer.cpp
    ${SERVER_DIR}/src/RedisDatabase.cpp
    ${SERVER_DIR}/src/RedisCommandHandler.cpp
    ${EVICTION_SOURCES}
)
target_include_directories(my_redis_server PRIVATE ${SERVER_DIR}/include)
target_link_libraries(my_redis_server PRIVATE Threads::Threads)