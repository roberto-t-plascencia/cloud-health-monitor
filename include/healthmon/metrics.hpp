#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "healthmon/config.hpp"
#include "healthmon/probe.hpp"

namespace healthmon {

enum class HealthState { Unknown, Up, Degraded, Down };

std::string_view toString(HealthState state);

// Fixed-capacity ring buffer of recent latencies. Percentiles are computed
// over this sliding window so old data ages out.
class LatencyWindow {
public:
    explicit LatencyWindow(std::size_t capacity = 256);
    void add(std::chrono::milliseconds latency);
    // Nearest-rank percentile, p in [0, 100]. Returns 0ms when empty.
    std::chrono::milliseconds percentile(double p) const;
    std::size_t size() const { return samples_.size(); }

private:
    std::size_t capacity_;
    std::size_t next_ = 0;
    std::vector<std::int64_t> samples_;
};

// Immutable copy of one target's metrics, safe to read without locks.
struct TargetSnapshot {
    std::string name;
    std::string url;
    HealthState state = HealthState::Unknown;
    std::uint64_t checks_total = 0;
    std::uint64_t checks_failed = 0;
    std::uint64_t retries_total = 0;
    int consecutive_failures = 0;
    int last_status_code = 0;
    std::chrono::milliseconds last_latency{0};
    std::string last_error;
    std::chrono::milliseconds p50{0};
    std::chrono::milliseconds p95{0};
    std::chrono::milliseconds p99{0};
    double uptime_ratio = 0.0;  // successful checks / total checks
};

struct StateChange {
    std::string target;
    HealthState from;
    HealthState to;
};

// Thread-safe store of per-target health. Workers call record(); the HTTP
// server calls snapshot(). One mutex is plenty at this scale: record() is
// O(1) and percentiles are computed only on snapshot.
class MetricsRegistry {
public:
    MetricsRegistry(const std::vector<Target>& targets, int failure_threshold);

    // Records the outcome of one check (after retries). Returns the state
    // transition if the target's health state changed.
    std::optional<StateChange> record(const std::string& target, const ProbeResult& result,
                                      int attempts);

    std::vector<TargetSnapshot> snapshot() const;

    // True when every target has been checked at least once.
    bool allChecked() const;

private:
    struct Entry {
        Target target;
        HealthState state = HealthState::Unknown;
        std::uint64_t checks_total = 0;
        std::uint64_t checks_failed = 0;
        std::uint64_t retries_total = 0;
        int consecutive_failures = 0;
        int last_status_code = 0;
        std::chrono::milliseconds last_latency{0};
        std::string last_error;
        LatencyWindow latencies;
    };

    HealthState evaluate(const Entry& entry) const;

    int failure_threshold_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
};

}  // namespace healthmon
