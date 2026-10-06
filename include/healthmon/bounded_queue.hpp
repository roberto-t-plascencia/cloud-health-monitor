#pragma once
// Thread-safe bounded FIFO used to hand work to the thread pool.
// Two condition variables avoid waking the wrong side: producers wait on
// not_full_, consumers wait on not_empty_. wait() with a predicate handles
// spurious wakeups. We notify after releasing the lock so the woken thread
// does not immediately block on the mutex.
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace healthmon {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    bool push(T item) {
        {
            std::unique_lock lock(mutex_);
            not_full_.wait(lock, [&] { return closed_ || items_.size() < capacity_; });
            if (closed_) return false;
            items_.push_back(std::move(item));
        }
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::optional<T> item;
        {
            std::unique_lock lock(mutex_);
            not_empty_.wait(lock, [&] { return closed_ || !items_.empty(); });
            if (items_.empty()) return std::nullopt;  // closed and drained
            item.emplace(std::move(items_.front()));
            items_.pop_front();
        }
        not_full_.notify_one();
        return item;
    }

    std::optional<T> tryPop() {
        std::optional<T> item;
        {
            std::lock_guard lock(mutex_);
            if (items_.empty()) return std::nullopt;
            item.emplace(std::move(items_.front()));
            items_.pop_front();
        }
        not_full_.notify_one();
        return item;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> items_;
    bool closed_ = false;
};

}  // namespace healthmon
