#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "healthmon/bounded_queue.hpp"

namespace healthmon {

// Fixed-size worker pool fed by a bounded queue. submit() blocks when the
// queue is full, which gives natural backpressure instead of unbounded memory
// growth. shutdown() stops accepting work, lets workers drain what is already
// queued, and joins them.
class ThreadPool {
public:
    using Job = std::function<void()>;

    ThreadPool(std::size_t threads, std::size_t queue_capacity) : queue_(queue_capacity) {
        if (threads == 0) threads = 1;
        workers_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                while (auto job = queue_.pop()) {
                    try {
                        (*job)();
                    } catch (...) {
                        // A failing job must never take down a worker thread.
                    }
                }
            });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Returns false if the pool is shutting down.
    bool submit(Job job) { return queue_.push(std::move(job)); }

    void shutdown() {
        std::call_once(shutdown_once_, [this] {
            queue_.close();
            workers_.clear();  // std::jthread joins on destruction
        });
    }

    std::size_t size() const { return workers_.size(); }

private:
    BoundedQueue<Job> queue_;
    std::vector<std::jthread> workers_;
    std::once_flag shutdown_once_;
};

}  // namespace healthmon
