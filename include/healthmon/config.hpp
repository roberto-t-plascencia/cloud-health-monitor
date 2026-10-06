#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace healthmon {

struct RetryConfig {
    int max_attempts = 3;                            // total tries per check, including the first
    std::chrono::milliseconds base_delay{200};       // first backoff delay
    std::chrono::milliseconds max_delay{2000};       // backoff cap
};

struct Target {
    std::string name;
    std::string url;
    std::chrono::milliseconds interval{15000};
    std::chrono::milliseconds timeout{3000};
    int expected_status = 200;
    std::chrono::milliseconds degraded_latency{1000};  // p95 above this marks the target DEGRADED
    RetryConfig retry;
};

struct Config {
    std::vector<Target> targets;
    std::uint16_t metrics_port = 9100;
    std::size_t workers = 4;
    int failure_threshold = 3;   // consecutive failed checks before a target is DOWN
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses and validates a JSON config document. Throws ConfigError with a
// message that names the offending field.
Config parseConfig(const std::string& json_text);

// Reads a file and calls parseConfig.
Config loadConfig(const std::string& path);

}  // namespace healthmon
