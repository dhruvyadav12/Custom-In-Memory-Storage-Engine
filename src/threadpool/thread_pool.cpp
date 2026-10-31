#include "threadpool/thread_pool.hpp"

namespace storage {

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = 1;
    }
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_task_.wait(lock, [this]() {
                return stop_.load() || !tasks_.empty();
            });

            if (stop_.load() && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            active_tasks_.fetch_add(1);
        }

        try {
            task();
        } catch (...) {
            // Suppress uncaught exceptions from worker task to keep thread alive
        }

        active_tasks_.fetch_sub(1);
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (tasks_.empty() && active_tasks_.load() == 0) {
                cv_finished_.notify_all();
            }
        }
    }
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    cv_finished_.wait(lock, [this]() {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}

void ThreadPool::shutdown() {
    bool expected = false;
    if (stop_.compare_exchange_strong(expected, true)) {
        cv_task_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

size_t ThreadPool::pending_tasks() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

} // namespace storage
