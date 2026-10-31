#include "threadpool/thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

void test_thread_pool_basic() {
    std::cout << "[Test] ThreadPool: Task Enqueue & Futures... ";
    storage::ThreadPool pool(4);

    auto f1 = pool.enqueue([]() { return 42; });
    auto f2 = pool.enqueue([](int a, int b) { return a + b; }, 10, 25);
    auto f3 = pool.enqueue([]() { return std::string("ThreadPool Ready"); });

    assert(f1.get() == 42);
    assert(f2.get() == 35);
    assert(f3.get() == "ThreadPool Ready");

    std::cout << "PASSED\n";
}

void test_thread_pool_concurrency_stress() {
    std::cout << "[Test] ThreadPool: Concurrent Tasks Stress Test... ";
    storage::ThreadPool pool(8);

    constexpr int NUM_TASKS = 5000;
    std::atomic<int> counter{0};

    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.enqueue([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait_all();
    assert(counter.load() == NUM_TASKS);
    std::cout << "PASSED (" << NUM_TASKS << " tasks executed)\n";
}

int main() {
    std::cout << "=== Running ThreadPool Unit Tests ===\n";
    test_thread_pool_basic();
    test_thread_pool_concurrency_stress();
    std::cout << "ALL ThreadPool Tests Passed Successfully!\n";
    return 0;
}
