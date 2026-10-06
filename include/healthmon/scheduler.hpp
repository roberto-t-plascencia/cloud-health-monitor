#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "healthmon/config.hpp"
#include "healthmon/metrics.hpp"
#include "healthmon/probe.hpp"
#include "healthmon/thread_pool.hpp"

namespace healthmon {

// Decides WHEN each target is checked and hands the work to the thread pool.
//
// - A min-heap ordered by next-due time means the scheduler thread sleeps
//   until exactly the next check is due (no busy polling).
// - Next run = previous due time + interval, so checks do not drift.
// - Start times are staggered across each interval to avoid load spikes.
// - A target is never probed twice at once: if the previous check (with its
//   retries) is still running, that tick is skipped and counted.
// - stop() interrupts retry backoff sleeps and waits for in-flight checks, so
//   shutdown on SIGTERM is quick and clean (important in Kubernetes).
class Scheduler {
public:
    Scheduler(const Config& config, Prober& prober, MetricsRegistry& registry, ThreadPool& pool);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void start();
    void stop();

    // Checks every target once (with retries) and blocks until all finish.
    void runOnce();

    std::size_t skippedTicks() const;

private:
    using Clock = std::chrono::steady_clock;
    struct Due {
        Clock::time_point when;
        std::size_t index;
        bool operator>(const Due& other) const { return when > other.when; }
    };

    void loop();
    // Returns false if the job could not be submitted.
    bool dispatch(std::size_t index);
    void runCheck(std::size_t index);
    void interruptibleSleep(std::chrono::milliseconds duration);

    Config config_;
    Prober& prober_;
    MetricsRegistry& registry_;
    ThreadPool& pool_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Due, std::vector<Due>, std::greater<>> heap_;
    std::vector<bool> in_flight_;
    std::size_t pending_ = 0;
    std::size_t skipped_ = 0;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace healthmon
