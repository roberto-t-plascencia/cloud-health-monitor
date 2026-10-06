#include "healthmon/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>

using healthmon::ThreadPool;
using namespace std::chrono_literals;

TEST(ThreadPool, RunsAllSubmittedJobsBeforeShutdownCompletes) {
    std::atomic<int> done{0};
    {
        ThreadPool pool(4, 8);
        for (int i = 0; i < 100; ++i) ASSERT_TRUE(pool.submit([&] { ++done; }));
    }  // destructor drains the queue and joins
    EXPECT_EQ(done.load(), 100);
}

TEST(ThreadPool, RejectsWorkAfterShutdown) {
    ThreadPool pool(2, 4);
    pool.shutdown();
    EXPECT_FALSE(pool.submit([] {}));
    pool.shutdown();  // idempotent
}

TEST(ThreadPool, ThrowingJobDoesNotKillWorker) {
    std::atomic<int> done{0};
    ThreadPool pool(1, 4);
    pool.submit([] { throw std::runtime_error("boom"); });
    pool.submit([&] { ++done; });
    pool.shutdown();
    EXPECT_EQ(done.load(), 1);
}

TEST(ThreadPool, RunsJobsInParallel) {
    ThreadPool pool(4, 8);
    std::atomic<int> active{0}, peak{0};
    for (int i = 0; i < 4; ++i)
        pool.submit([&] {
            int now = ++active;
            int prev = peak.load();
            while (now > prev && !peak.compare_exchange_weak(prev, now)) {
            }
            std::this_thread::sleep_for(100ms);
            --active;
        });
    pool.shutdown();
    EXPECT_GE(peak.load(), 2);
}
