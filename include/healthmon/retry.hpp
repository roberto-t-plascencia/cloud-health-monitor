#pragma once

#include <chrono>
#include <functional>
#include <random>

#include "healthmon/config.hpp"
#include "healthmon/probe.hpp"

namespace healthmon {

// Exponential backoff with "full jitter": a random delay in
// [0, min(max_delay, base_delay * 2^(attempt-1))]. Jitter keeps many monitors
// from retrying a struggling service in lockstep (the thundering herd).
// `attempt` is 1 for the delay after the first failure.
std::chrono::milliseconds backoffDelay(const RetryConfig& retry, int attempt, std::mt19937& rng);

struct RetryOutcome {
    ProbeResult result;   // result of the last attempt
    int attempts = 0;
};

using Sleeper = std::function<void(std::chrono::milliseconds)>;
// Returns true if the caller should stop retrying (e.g. shutdown requested).
using StopCheck = std::function<bool()>;

// Calls `attempt_fn` until it succeeds, max_attempts is reached, or stop()
// returns true. `sleep` is injected so tests run instantly.
RetryOutcome runWithRetry(const RetryConfig& retry, const std::function<ProbeResult()>& attempt_fn,
                          const Sleeper& sleep, std::mt19937& rng,
                          const StopCheck& stop = [] { return false; });

}  // namespace healthmon
