#include "healthmon/bounded_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using healthmon::BoundedQueue;

TEST(BoundedQueue, FifoAndClose) {
    BoundedQueue<int> q(3);
    ASSERT_TRUE(q.push(1));
    ASSERT_TRUE(q.push(2));
    q.close();
    EXPECT_FALSE(q.push(3));
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_FALSE(q.pop().has_value());
}

TEST(BoundedQueue, ConcurrentProducersAndConsumers) {
    BoundedQueue<int> q(8);
    std::atomic<long long> sum{0};
    std::vector<std::thread> consumers;
    for (int i = 0; i < 3; ++i)
        consumers.emplace_back([&] {
            while (auto v = q.pop()) sum += *v;
        });
    std::vector<std::thread> producers;
    for (int p = 0; p < 3; ++p)
        producers.emplace_back([&] {
            for (int i = 1; i <= 1000; ++i) q.push(i);
        });
    for (auto& t : producers) t.join();
    q.close();
    for (auto& t : consumers) t.join();
    EXPECT_EQ(sum.load(), 3LL * 1000 * 1001 / 2);
}
