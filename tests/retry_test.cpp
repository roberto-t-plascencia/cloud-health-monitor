#include "healthmon/retry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "test_helpers.hpp"

using namespace healthmon;
using namespace healthmon::testing;
using std::chrono::milliseconds;

namespace {
RetryConfig retryConfig(int attempts, int base_ms, int max_ms) {
    RetryConfig r;
    r.max_attempts = attempts;
    r.base_delay = milliseconds{base_ms};
    r.max_delay = milliseconds{max_ms};
    return r;
}
}  // namespace

TEST(Backoff, CeilingDoublesAndIsCapped) {
    std::mt19937 rng(42);
    auto r = retryConfig(5, 100, 500);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_LE(backoffDelay(r, 1, rng), milliseconds{100});
        EXPECT_LE(backoffDelay(r, 2, rng), milliseconds{200});
        EXPECT_LE(backoffDelay(r, 3, rng), milliseconds{400});
        EXPECT_LE(backoffDelay(r, 4, rng), milliseconds{500});   // capped
        EXPECT_LE(backoffDelay(r, 60, rng), milliseconds{500});  // no overflow
        EXPECT_GE(backoffDelay(r, 3, rng), milliseconds{0});
    }
}

TEST(Backoff, JitterSpreadsDelays) {
    std::mt19937 rng(7);
    auto r = retryConfig(3, 1000, 1000);
    std::int64_t min_seen = 1000, max_seen = 0;
    for (int i = 0; i < 500; ++i) {
        std::int64_t d = backoffDelay(r, 1, rng).count();
        min_seen = std::min(min_seen, d);
        max_seen = std::max(max_seen, d);
    }
    EXPECT_LT(min_seen, 200);
    EXPECT_GT(max_seen, 800);
}

TEST(RunWithRetry, StopsOnFirstSuccess) {
    std::mt19937 rng(1);
    int calls = 0;
    std::vector<milliseconds> sleeps;
    auto outcome = runWithRetry(
        retryConfig(3, 10, 100), [&] { return ++calls == 1 ? ok() : fail(); },
        [&](milliseconds d) { sleeps.push_back(d); }, rng);
    EXPECT_TRUE(outcome.result.ok);
    EXPECT_EQ(outcome.attempts, 1);
    EXPECT_TRUE(sleeps.empty());
}

TEST(RunWithRetry, RetriesUntilSuccess) {
    std::mt19937 rng(1);
    int calls = 0;
    int sleeps = 0;
    auto outcome = runWithRetry(
        retryConfig(5, 10, 100), [&] { return ++calls < 3 ? fail() : ok(); },
        [&](milliseconds) { ++sleeps; }, rng);
    EXPECT_TRUE(outcome.result.ok);
    EXPECT_EQ(outcome.attempts, 3);
    EXPECT_EQ(sleeps, 2);
}

TEST(RunWithRetry, GivesUpAfterMaxAttemptsWithoutTrailingSleep) {
    std::mt19937 rng(1);
    int calls = 0, sleeps = 0;
    auto outcome = runWithRetry(
        retryConfig(3, 10, 100), [&] { ++calls; return fail("timeout"); },
        [&](milliseconds) { ++sleeps; }, rng);
    EXPECT_FALSE(outcome.result.ok);
    EXPECT_EQ(outcome.result.error, "timeout");
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(sleeps, 2);
}

TEST(RunWithRetry, StopCheckAbortsRetries) {
    std::mt19937 rng(1);
    int calls = 0;
    bool stop = false;
    auto outcome = runWithRetry(
        retryConfig(10, 10, 100), [&] { ++calls; return fail(); },
        [&](milliseconds) { stop = true; }, rng, [&] { return stop; });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(outcome.attempts, 1);
}
