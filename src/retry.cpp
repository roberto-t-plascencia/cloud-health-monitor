#include "healthmon/retry.hpp"

#include <algorithm>

namespace healthmon {

std::chrono::milliseconds backoffDelay(const RetryConfig& retry, int attempt, std::mt19937& rng) {
    const long long base = retry.base_delay.count();
    const long long cap = retry.max_delay.count();
    // Double the ceiling each attempt, stopping once we pass the cap to avoid overflow.
    long long ceiling = base;
    for (int i = 1; i < attempt && ceiling < cap; ++i) ceiling *= 2;
    ceiling = std::min(ceiling, cap);

    std::uniform_int_distribution<long long> dist(0, ceiling);
    return std::chrono::milliseconds{dist(rng)};
}

RetryOutcome runWithRetry(const RetryConfig& retry, const std::function<ProbeResult()>& attempt_fn,
                          const Sleeper& sleep, std::mt19937& rng, const StopCheck& stop) {
    RetryOutcome outcome;
    for (int attempt = 1; attempt <= retry.max_attempts; ++attempt) {
        outcome.result = attempt_fn();
        outcome.attempts = attempt;
        if (outcome.result.ok || attempt == retry.max_attempts || stop()) break;
        sleep(backoffDelay(retry, attempt, rng));
        if (stop()) break;
    }
    return outcome;
}

}  // namespace healthmon
