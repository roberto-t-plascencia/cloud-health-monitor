#include "healthmon/config.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace healthmon;
using std::chrono::milliseconds;

namespace {

void expectError(const std::string& json, const std::string& fragment) {
    try {
        parseConfig(json);
        FAIL() << "expected ConfigError containing: " << fragment;
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos)
            << "message was: " << e.what();
    }
}

}  // namespace

TEST(Config, ParsesFullDocument) {
    auto config = parseConfig(R"({
        "metrics_port": 9200, "workers": 8, "failure_threshold": 2,
        "targets": [{
            "name": "api", "url": "https://api.example.com/health",
            "interval_ms": 5000, "timeout_ms": 1000, "expected_status": 204,
            "degraded_latency_ms": 300,
            "retry": {"max_attempts": 4, "base_delay_ms": 100, "max_delay_ms": 800}
        }]
    })");
    EXPECT_EQ(config.metrics_port, 9200);
    EXPECT_EQ(config.workers, 8u);
    EXPECT_EQ(config.failure_threshold, 2);
    ASSERT_EQ(config.targets.size(), 1u);
    const auto& t = config.targets[0];
    EXPECT_EQ(t.name, "api");
    EXPECT_EQ(t.interval, milliseconds{5000});
    EXPECT_EQ(t.timeout, milliseconds{1000});
    EXPECT_EQ(t.expected_status, 204);
    EXPECT_EQ(t.degraded_latency, milliseconds{300});
    EXPECT_EQ(t.retry.max_attempts, 4);
    EXPECT_EQ(t.retry.base_delay, milliseconds{100});
    EXPECT_EQ(t.retry.max_delay, milliseconds{800});
}

TEST(Config, AppliesDefaults) {
    auto config = parseConfig(R"({"targets":[{"name":"a","url":"http://a.local"}]})");
    EXPECT_EQ(config.metrics_port, 9100);
    EXPECT_EQ(config.workers, 4u);
    EXPECT_EQ(config.targets[0].expected_status, 200);
    EXPECT_EQ(config.targets[0].retry.max_attempts, 3);
}

TEST(Config, RejectsInvalidDocuments) {
    expectError("{not json", "invalid JSON");
    expectError("[]", "root must be an object");
    expectError(R"({"targets":[]})", "non-empty array");
    expectError(R"({"targets":[{"url":"http://a"}]})", "targets[0].name is required");
    expectError(R"({"targets":[{"name":"a","url":"ftp://a"}]})", "must start with http");
    expectError(R"({"targets":[{"name":"a","url":"http://a","interval_ms":0}]})", "interval_ms must be greater than 0");
    expectError(R"({"targets":[{"name":"a","url":"http://a","interval_ms":"fast"}]})", "wrong type");
    expectError(R"({"targets":[{"name":"a","url":"http://a","interval_ms":1000,"timeout_ms":2000}]})", "must not exceed interval_ms");
    expectError(R"({"targets":[{"name":"a","url":"http://a","expected_status":999}]})", "valid HTTP status");
    expectError(R"({"targets":[{"name":"a","url":"http://a"},{"name":"a","url":"http://b"}]})", "duplicate target name");
    expectError(R"({"workers":0,"targets":[{"name":"a","url":"http://a"}]})", "workers must be between");
    expectError(R"({"metrics_port":70000,"targets":[{"name":"a","url":"http://a"}]})", "metrics_port");
    expectError(R"({"targets":[{"name":"a","url":"http://a","retry":{"max_attempts":0}}]})", "max_attempts");
    expectError(R"({"targets":[{"name":"a","url":"http://a","retry":{"base_delay_ms":500,"max_delay_ms":100}}]})", "max_delay_ms must be >=");
}

TEST(Config, LoadConfigReportsMissingFile) {
    EXPECT_THROW(loadConfig("/definitely/not/here.json"), ConfigError);
}
