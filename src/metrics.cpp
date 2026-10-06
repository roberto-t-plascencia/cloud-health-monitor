#include "healthmon/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace healthmon {

std::string_view toString(HealthState state) {
    switch (state) {
        case HealthState::Up: return "UP";
        case HealthState::Degraded: return "DEGRADED";
        case HealthState::Down: return "DOWN";
        case HealthState::Unknown: break;
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------- LatencyWindow

LatencyWindow::LatencyWindow(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {
    samples_.reserve(capacity_);
}

void LatencyWindow::add(std::chrono::milliseconds latency) {
    if (samples_.size() < capacity_) {
        samples_.push_back(latency.count());
    } else {
        samples_[next_] = latency.count();  // overwrite the oldest sample
    }
    next_ = (next_ + 1) % capacity_;
}

std::chrono::milliseconds LatencyWindow::percentile(double p) const {
    if (samples_.empty()) return std::chrono::milliseconds{0};
    std::vector<std::int64_t> sorted = samples_;  // copy: window keeps insertion order
    const auto n = sorted.size();
    auto rank = static_cast<std::size_t>(std::ceil(std::clamp(p, 0.0, 100.0) / 100.0 *
                                                   static_cast<double>(n)));
    rank = std::clamp<std::size_t>(rank, 1, n);
    auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(rank - 1);
    std::nth_element(sorted.begin(), nth, sorted.end());
    return std::chrono::milliseconds{*nth};
}

// -------------------------------------------------------------- MetricsRegistry

MetricsRegistry::MetricsRegistry(const std::vector<Target>& targets, int failure_threshold)
    : failure_threshold_(failure_threshold) {
    for (const auto& t : targets) {
        Entry entry;
        entry.target = t;
        entries_.emplace(t.name, std::move(entry));
    }
}

HealthState MetricsRegistry::evaluate(const Entry& e) const {
    if (e.consecutive_failures >= failure_threshold_) return HealthState::Down;
    if (e.consecutive_failures > 0) return HealthState::Degraded;  // failing, not yet DOWN
    if (e.latencies.percentile(95) > e.target.degraded_latency) return HealthState::Degraded;
    return HealthState::Up;
}

std::optional<StateChange> MetricsRegistry::record(const std::string& target,
                                                   const ProbeResult& result, int attempts) {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(target);
    if (it == entries_.end()) throw std::invalid_argument("unknown target: " + target);
    Entry& e = it->second;

    ++e.checks_total;
    e.retries_total += static_cast<std::uint64_t>(std::max(0, attempts - 1));
    e.last_status_code = result.status_code;
    e.last_latency = result.latency;
    e.last_error = result.error;
    if (result.ok) {
        e.consecutive_failures = 0;
        e.latencies.add(result.latency);  // only successful checks: timeouts would skew latency
    } else {
        ++e.checks_failed;
        ++e.consecutive_failures;
    }

    HealthState next = evaluate(e);
    if (next == e.state) return std::nullopt;
    StateChange change{target, e.state, next};
    e.state = next;
    return change;
}

std::vector<TargetSnapshot> MetricsRegistry::snapshot() const {
    std::lock_guard lock(mutex_);
    std::vector<TargetSnapshot> out;
    out.reserve(entries_.size());
    for (const auto& [name, e] : entries_) {
        TargetSnapshot s;
        s.name = name;
        s.url = e.target.url;
        s.state = e.state;
        s.checks_total = e.checks_total;
        s.checks_failed = e.checks_failed;
        s.retries_total = e.retries_total;
        s.consecutive_failures = e.consecutive_failures;
        s.last_status_code = e.last_status_code;
        s.last_latency = e.last_latency;
        s.last_error = e.last_error;
        s.p50 = e.latencies.percentile(50);
        s.p95 = e.latencies.percentile(95);
        s.p99 = e.latencies.percentile(99);
        s.uptime_ratio = e.checks_total == 0
                             ? 0.0
                             : static_cast<double>(e.checks_total - e.checks_failed) /
                                   static_cast<double>(e.checks_total);
        out.push_back(std::move(s));
    }
    return out;
}

bool MetricsRegistry::allChecked() const {
    std::lock_guard lock(mutex_);
    return std::all_of(entries_.begin(), entries_.end(),
                       [](const auto& kv) { return kv.second.checks_total > 0; });
}

}  // namespace healthmon
