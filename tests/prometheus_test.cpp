#include "healthmon/prometheus.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace healthmon;
using std::chrono::milliseconds;

namespace {
TargetSnapshot sample() {
    TargetSnapshot s;
    s.name = "api";
    s.url = "https://api.example.com/health";
    s.state = HealthState::Up;
    s.checks_total = 10;
    s.checks_failed = 1;
    s.retries_total = 2;
    s.p50 = milliseconds{120};
    s.p95 = milliseconds{450};
    s.p99 = milliseconds{900};
    s.uptime_ratio = 0.9;
    s.last_status_code = 200;
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
}  // namespace

TEST(Prometheus, RendersHelpTypeAndSamples) {
    auto text = renderPrometheus({sample()});
    const std::string l = R"(target="api",url="https://api.example.com/health")";
    EXPECT_TRUE(contains(text, "# TYPE healthmon_checks_total counter\n"));
    EXPECT_TRUE(contains(text, "# HELP healthmon_target_up "));
    EXPECT_TRUE(contains(text, "healthmon_target_up{" + l + "} 1\n"));
    EXPECT_TRUE(contains(text, "healthmon_checks_total{" + l + "} 10\n"));
    EXPECT_TRUE(contains(text, "healthmon_check_failures_total{" + l + "} 1\n"));
    EXPECT_TRUE(contains(text, "healthmon_retries_total{" + l + "} 2\n"));
    EXPECT_TRUE(contains(text, "healthmon_latency_seconds{" + l + ",quantile=\"0.95\"} 0.45\n"));
    EXPECT_TRUE(contains(text, "healthmon_target_state{" + l + ",state=\"UP\"} 1\n"));
    EXPECT_TRUE(contains(text, "healthmon_target_state{" + l + ",state=\"DOWN\"} 0\n"));
    EXPECT_TRUE(contains(text, "healthmon_uptime_ratio{" + l + "} 0.9\n"));
}

TEST(Prometheus, UncheckedTargetIsNotUp) {
    TargetSnapshot s;
    s.name = "new";
    s.url = "http://x";
    EXPECT_TRUE(contains(renderPrometheus({s}), R"(healthmon_target_up{target="new",url="http://x"} 0)"));
}

TEST(Prometheus, EscapesLabelValues) {
    EXPECT_EQ(escapeLabelValue(R"(a"b\c)" "\n"), R"(a\"b\\c\n)");
}

TEST(StatusJson, IsValidJsonWithAllTargets) {
    auto doc = nlohmann::json::parse(renderStatusJson({sample()}));
    ASSERT_EQ(doc["targets"].size(), 1u);
    EXPECT_EQ(doc["targets"][0]["name"], "api");
    EXPECT_EQ(doc["targets"][0]["state"], "UP");
    EXPECT_EQ(doc["targets"][0]["p95_ms"], 450);
}
