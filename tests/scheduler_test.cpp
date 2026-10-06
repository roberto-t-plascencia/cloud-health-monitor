#include "healthmon/scheduler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "healthmon/logger.hpp"
#include "test_helpers.hpp"

using namespace healthmon;
using namespace healthmon::testing;
using namespace std::chrono_literals;

namespace {

Config makeConfig(std::vector<Target> targets) {
    Config c;
    c.targets = std::move(targets);
    c.workers = 4;
    c.failure_threshold = 3;
    return c;
}

class SchedulerTest : public ::testing::Test {
protected:
    void SetUp() override { setLoggingEnabled(false); }
    void TearDown() override { setLoggingEnabled(true); }
};

}  // namespace

TEST_F(SchedulerTest, RunOnceChecksEveryTargetExactlyOnce) {
    auto config = makeConfig({makeTarget("a"), makeTarget("b"), makeTarget("c")});
    FakeProber prober;
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.runOnce();
    EXPECT_EQ(prober.calls(), 3);
    for (const auto& s : registry.snapshot()) {
        EXPECT_EQ(s.checks_total, 1u);
        EXPECT_EQ(s.state, HealthState::Up);
    }
}

TEST_F(SchedulerTest, ChecksRepeatOnInterval) {
    auto config = makeConfig({makeTarget("a", 50ms)});
    FakeProber prober;
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.start();
    std::this_thread::sleep_for(330ms);
    scheduler.stop();
    // Expect about 7 checks (t = 0, 50, ..., 300ms); allow slack for slow CI.
    EXPECT_GE(prober.calls(), 4);
    EXPECT_LE(prober.calls(), 9);
}

TEST_F(SchedulerTest, NeverProbesSameTargetConcurrently) {
    auto config = makeConfig({makeTarget("slow", 30ms)});
    FakeProber prober(true, 120ms);  // each probe takes 4 intervals
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.start();
    std::this_thread::sleep_for(400ms);
    scheduler.stop();
    EXPECT_EQ(prober.maxActive(), 1);
    EXPECT_GT(scheduler.skippedTicks(), 0u);
}

TEST_F(SchedulerTest, DifferentTargetsRunInParallel) {
    auto config = makeConfig({makeTarget("a", 500ms), makeTarget("b", 500ms), makeTarget("c", 500ms)});
    FakeProber prober(true, 100ms);
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.runOnce();
    EXPECT_GE(prober.maxActive(), 2);
}

TEST_F(SchedulerTest, RepeatedFailuresMarkTargetDown) {
    auto config = makeConfig({makeTarget("api")});
    FakeProber prober(false);
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.runOnce();
    EXPECT_EQ(registry.snapshot()[0].state, HealthState::Degraded);
    scheduler.runOnce();
    scheduler.runOnce();
    EXPECT_EQ(registry.snapshot()[0].state, HealthState::Down);

    prober.setSucceed(true);
    scheduler.runOnce();
    EXPECT_EQ(registry.snapshot()[0].state, HealthState::Up);
}

TEST_F(SchedulerTest, StopInterruptsRetryBackoff) {
    auto target = makeTarget("flaky", 10s);
    target.timeout = 1s;
    target.retry.max_attempts = 5;
    target.retry.base_delay = 5s;  // without interruption this would take ~20s
    target.retry.max_delay = 5s;
    auto config = makeConfig({target});
    FakeProber prober(false);
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, 8);
    Scheduler scheduler(config, prober, registry, pool);

    scheduler.start();
    std::this_thread::sleep_for(100ms);  // first attempt fails, now sleeping in backoff
    auto begin = std::chrono::steady_clock::now();
    scheduler.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 1s);
    EXPECT_EQ(registry.snapshot()[0].checks_total, 1u);  // the check was still recorded
}
