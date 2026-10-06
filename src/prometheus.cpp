#include "healthmon/prometheus.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

namespace healthmon {
namespace {

void header(std::ostringstream& out, const char* name, const char* type, const char* help) {
    out << "# HELP " << name << ' ' << help << '\n' << "# TYPE " << name << ' ' << type << '\n';
}

std::string labels(const TargetSnapshot& s) {
    return "target=\"" + escapeLabelValue(s.name) + "\",url=\"" + escapeLabelValue(s.url) + "\"";
}

}  // namespace

std::string escapeLabelValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

std::string renderPrometheus(const std::vector<TargetSnapshot>& snapshots) {
    std::ostringstream out;

    header(out, "healthmon_target_up", "gauge", "1 if the last check succeeded, else 0.");
    for (const auto& s : snapshots)
        out << "healthmon_target_up{" << labels(s) << "} "
            << (s.checks_total > 0 && s.consecutive_failures == 0 ? 1 : 0) << '\n';

    header(out, "healthmon_target_state", "gauge",
           "Current health state; the series with value 1 is the active state.");
    for (const auto& s : snapshots)
        for (auto state : {HealthState::Up, HealthState::Degraded, HealthState::Down,
                           HealthState::Unknown})
            out << "healthmon_target_state{" << labels(s) << ",state=\"" << toString(state)
                << "\"} " << (s.state == state ? 1 : 0) << '\n';

    header(out, "healthmon_checks_total", "counter", "Total checks performed.");
    for (const auto& s : snapshots)
        out << "healthmon_checks_total{" << labels(s) << "} " << s.checks_total << '\n';

    header(out, "healthmon_check_failures_total", "counter", "Checks that failed after retries.");
    for (const auto& s : snapshots)
        out << "healthmon_check_failures_total{" << labels(s) << "} " << s.checks_failed << '\n';

    header(out, "healthmon_retries_total", "counter", "Retry attempts beyond the first try.");
    for (const auto& s : snapshots)
        out << "healthmon_retries_total{" << labels(s) << "} " << s.retries_total << '\n';

    header(out, "healthmon_latency_seconds", "gauge",
           "Latency percentiles over the recent window of successful checks.");
    for (const auto& s : snapshots) {
        auto emit = [&](const char* q, std::chrono::milliseconds v) {
            out << "healthmon_latency_seconds{" << labels(s) << ",quantile=\"" << q << "\"} "
                << static_cast<double>(v.count()) / 1000.0 << '\n';
        };
        emit("0.5", s.p50);
        emit("0.95", s.p95);
        emit("0.99", s.p99);
    }

    header(out, "healthmon_uptime_ratio", "gauge", "Successful checks divided by total checks.");
    for (const auto& s : snapshots)
        out << "healthmon_uptime_ratio{" << labels(s) << "} " << s.uptime_ratio << '\n';

    return out.str();
}

std::string renderStatusJson(const std::vector<TargetSnapshot>& snapshots) {
    nlohmann::json doc;
    doc["targets"] = nlohmann::json::array();
    for (const auto& s : snapshots) {
        doc["targets"].push_back({
            {"name", s.name},
            {"url", s.url},
            {"state", std::string(toString(s.state))},
            {"checks_total", s.checks_total},
            {"checks_failed", s.checks_failed},
            {"retries_total", s.retries_total},
            {"consecutive_failures", s.consecutive_failures},
            {"last_status_code", s.last_status_code},
            {"last_latency_ms", s.last_latency.count()},
            {"last_error", s.last_error},
            {"p50_ms", s.p50.count()},
            {"p95_ms", s.p95.count()},
            {"p99_ms", s.p99.count()},
            {"uptime_ratio", s.uptime_ratio},
        });
    }
    return doc.dump(2);
}

}  // namespace healthmon
