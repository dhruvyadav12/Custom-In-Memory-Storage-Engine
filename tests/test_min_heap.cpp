#include "datastructures/min_heap.hpp"
#include "storage/ttl_manager.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

void test_min_heap_basic() {
    std::cout << "[Test] MinHeap: Push & Pop Order... ";
    storage::MinHeap<int> heap;

    assert(heap.empty());
    heap.push(50);
    heap.push(20);
    heap.push(80);
    heap.push(10);
    heap.push(30);

    assert(heap.size() == 5);
    assert(heap.top() == 10);
    heap.pop();

    assert(heap.top() == 20);
    heap.pop();

    assert(heap.top() == 30);
    heap.pop();

    assert(heap.top() == 50);
    heap.pop();

    assert(heap.top() == 80);
    heap.pop();

    assert(heap.empty());
    std::cout << "PASSED\n";
}

void test_ttl_manager_expiration() {
    std::cout << "[Test] TTLManager: Min-Heap Based Key Expiration... ";
    storage::TTLManager ttl_mgr;

    // Set keys with different millisecond TTLs
    ttl_mgr.set_pttl("key_fast", 50);    // expires in 50ms
    ttl_mgr.set_pttl("key_medium", 200); // expires in 200ms
    ttl_mgr.set_pttl("key_slow", 500);   // expires in 500ms

    assert(ttl_mgr.has_ttl("key_fast"));
    assert(!ttl_mgr.is_expired("key_fast"));
    assert(ttl_mgr.get_pttl("key_fast") > 0);

    // Sleep 80ms -> key_fast should be expired
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    assert(ttl_mgr.is_expired("key_fast"));
    assert(!ttl_mgr.is_expired("key_medium"));
    assert(!ttl_mgr.is_expired("key_slow"));

    auto evicted = ttl_mgr.evict_expired(10);
    assert(evicted.size() == 1);
    assert(evicted[0] == "key_fast");

    // Sleep 150ms -> key_medium should be expired
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    evicted = ttl_mgr.evict_expired(10);
    assert(evicted.size() == 1);
    assert(evicted[0] == "key_medium");

    std::cout << "PASSED\n";
}

void test_ttl_invalidation_and_persist() {
    std::cout << "[Test] TTLManager: Key Update & Persist... ";
    storage::TTLManager ttl_mgr;

    ttl_mgr.set_pttl("key1", 50);
    assert(ttl_mgr.persist("key1"));
    assert(!ttl_mgr.has_ttl("key1"));
    assert(ttl_mgr.get_ttl("key1") == -1);

    // Overwrite key TTL with new version
    ttl_mgr.set_pttl("key2", 40);
    // Immediately renew TTL to 500ms
    ttl_mgr.set_pttl("key2", 500);

    // Sleep 70ms: old TTL (40ms) would have expired, but new TTL (500ms) is active
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    assert(!ttl_mgr.is_expired("key2"));

    auto evicted = ttl_mgr.evict_expired(10);
    assert(evicted.empty()); // Stale heap entry should be ignored

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running Min-Heap & TTL Unit Tests ===\n";
    test_min_heap_basic();
    test_ttl_manager_expiration();
    test_ttl_invalidation_and_persist();
    std::cout << "ALL Min-Heap & TTL Tests Passed Successfully!\n";
    return 0;
}
