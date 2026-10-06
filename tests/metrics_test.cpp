#include "healthmon/metrics.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "test_helpers.hpp"

using namespace healthmon;
using namespace healthmon::testing;
using std::chrono::milliseconds;

TEST(LatencyWindow, NearestRankPercentiles) {
    LatencyWindow w(100);
    for (int i = 1; i <= 100; ++i) w.add(milliseconds{i});
    EXPECT_EQ(w.percentile(50), milliseconds{50});
    EXPECT_EQ(w.percentile(95), milliseconds{95});
    EXPECT_EQ(w.percentile(99), milliseconds{99});
    EXPECT_EQ(w.percentile(100), milliseconds{100});
    EXPECT_EQ(w.percentile(0), milliseconds{1});
}

TEST(LatencyWindow, EmptyReturnsZero) {
    LatencyWindow w;
    EXPECT_EQ(w.percentile(95), milliseconds{0});
}

TEST(LatencyWindow, OldSamplesAgeOut) {
    LatencyWindow w(3);
    w.add(milliseconds{1000});
    w.add(milliseconds{1000});
    w.add(milliseconds{1000});
    w.add(milliseconds{10});
    w.add(milliseconds{10});
    w.add(milliseconds{10});  // all 1000ms samples overwritten
    EXPECT_EQ(w.size(), 3u);
    EXPECT_EQ(w.percentile(100), milliseconds{10});
}

class RegistryTest : public ::testing::Test {
protected:
    std::vector<Target> targets{makeTarget("api")};
    MetricsRegistry registry{targets, /*failure_threshold=*/3};
};

TEST_F(RegistryTest, FirstSuccessTransitionsUnknownToUp) {
    auto change = registry.record("api", ok(), 1);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->from, HealthState::Unknown);
    EXPECT_EQ(change->to, HealthState::Up);
    EXPECT_FALSE(registry.record("api", ok(), 1).has_value());  // no change, no event
}

TEST_F(RegistryTest, FailuresGoDegradedThenDownThenRecover) {
    registry.record("api", ok(), 1);
    EXPECT_EQ(registry.record("api", fail(), 3)->to, HealthState::Degraded);
    EXPECT_FALSE(registry.record("api", fail(), 3).has_value());     // still DEGRADED
    EXPECT_EQ(registry.record("api", fail(), 3)->to, HealthState::Down);  // 3rd in a row
    auto recovered = registry.record("api", ok(), 1);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->from, HealthState::Down);
    EXPECT_EQ(recovered->to, HealthState::Up);
}

TEST_F(RegistryTest, SlowP95MarksDegraded) {
    for (int i = 0; i < 18; ++i) registry.record("api", ok(50), 1);
    EXPECT_EQ(registry.snapshot()[0].state, HealthState::Up);
    // 2 of 20 samples (10%) above 500ms pushes p95 over the threshold
    registry.record("api", ok(900), 1);
    registry.record("api", ok(900), 1);
    EXPECT_EQ(registry.snapshot()[0].state, HealthState::Degraded);
}

TEST_F(RegistryTest, SnapshotCountsAndUptime) {
    registry.record("api", ok(20), 1);
    registry.record("api", ok(40), 2);
    registry.record("api", fail("timeout"), 3);
    registry.record("api", ok(30), 1);
    auto s = registry.snapshot().at(0);
    EXPECT_EQ(s.checks_total, 4u);
    EXPECT_EQ(s.checks_failed, 1u);
    EXPECT_EQ(s.retries_total, 3u);  // (2-1) + (3-1)
    EXPECT_EQ(s.consecutive_failures, 0);
    EXPECT_DOUBLE_EQ(s.uptime_ratio, 0.75);
    EXPECT_EQ(s.p50, milliseconds{30});  // failures are excluded from latency
    EXPECT_TRUE(registry.allChecked());
}

TEST_F(RegistryTest, UnknownTargetThrows) {
    EXPECT_THROW(registry.record("nope", ok(), 1), std::invalid_argument);
}

TEST(Registry, ConcurrentRecordAndSnapshot) {
    std::vector<Target> targets{makeTarget("a"), makeTarget("b")};
    MetricsRegistry registry(targets, 3);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i) {
                registry.record(t % 2 ? "a" : "b", i % 10 ? ok(i % 100) : fail(), 1);
                if (i % 50 == 0) (void)registry.snapshot();
            }
        });
    for (auto& th : threads) th.join();
    std::uint64_t total = 0;
    for (const auto& s : registry.snapshot()) total += s.checks_total;
    EXPECT_EQ(total, 2000u);
}
