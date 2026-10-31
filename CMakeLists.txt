cmake_minimum_required(VERSION 3.14)
project(CustomInMemoryStorageEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /EHsc /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# Platform specific libraries
set(EXTRA_LIBS "")
if(WIN32)
    set(EXTRA_LIBS ws2_32)
else()
    find_package(Threads REQUIRED)
    set(EXTRA_LIBS Threads::Threads)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core Library
set(CORE_SOURCES
    src/datastructures/avl_tree.hpp
    src/datastructures/min_heap.hpp
    src/network/socket.cpp
    src/network/connection.cpp
    src/network/event_loop.cpp
    src/protocol/tlv.cpp
    src/protocol/command.cpp
    src/storage/storage_engine.cpp
    src/storage/ttl_manager.cpp
    src/storage/persistence.cpp
    src/threadpool/thread_pool.cpp
)

# Collect existing source files
add_library(storage_core STATIC
    src/network/socket.cpp
    src/network/connection.cpp
    src/network/event_loop.cpp
    src/protocol/tlv.cpp
    src/protocol/command.cpp
    src/storage/storage_engine.cpp
    src/storage/ttl_manager.cpp
    src/storage/persistence.cpp
    src/threadpool/thread_pool.cpp
)

target_link_libraries(storage_core PUBLIC ${EXTRA_LIBS})

# Server Executable
add_executable(storage_server src/server_main.cpp)
target_link_libraries(storage_server PRIVATE storage_core)

# CLI Client Executable
add_executable(storage_client client/storage_client.cpp)
target_link_libraries(storage_client PRIVATE storage_core)

# Benchmark Executable
add_executable(bench_storage benchmark/bench_storage.cpp)
target_link_libraries(bench_storage PRIVATE storage_core)

# Tests
enable_testing()

add_executable(test_avl_tree tests/test_avl_tree.cpp)
target_link_libraries(test_avl_tree PRIVATE storage_core)
add_test(NAME AVLTreeTest COMMAND test_avl_tree)

add_executable(test_min_heap tests/test_min_heap.cpp)
target_link_libraries(test_min_heap PRIVATE storage_core)
add_test(NAME MinHeapTest COMMAND test_min_heap)

add_executable(test_tlv tests/test_tlv.cpp)
target_link_libraries(test_tlv PRIVATE storage_core)
add_test(NAME TLVTest COMMAND test_tlv)

add_executable(test_thread_pool tests/test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE storage_core)
add_test(NAME ThreadPoolTest COMMAND test_thread_pool)

add_executable(test_storage_engine tests/test_storage_engine.cpp)
target_link_libraries(test_storage_engine PRIVATE storage_core)
add_test(NAME StorageEngineTest COMMAND test_storage_engine)
