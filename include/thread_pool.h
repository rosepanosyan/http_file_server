#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * A small C++17 thread pool:
 * - submit(std::function<void()>) to enqueue work
 * - stop() for graceful shutdown
 *
 * This is intentionally minimal but production-style:
 * - RAII stop in destructor
 * - condition_variable for worker wakeup
 */
class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool();

    void submit(std::function<void()> task);
    void stop();

private:
    void worker_loop();

    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopping_{false};
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
};
