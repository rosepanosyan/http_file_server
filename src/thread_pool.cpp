#include "thread_pool.h"

#include <iostream>

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        thread_count = 1;
    }
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopping_) {
            // If we are already stopping, ignore new tasks.
            return;
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPool] task exception: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[ThreadPool] task exception: unknown\n";
        }
    }
}
