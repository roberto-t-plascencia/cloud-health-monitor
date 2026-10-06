#include "healthmon/config.hpp"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace healthmon {
namespace {

using json = nlohmann::json;
using std::chrono::milliseconds;

template <typename T>
T getOr(const json& obj, const char* key, T fallback, const std::string& where) {
    if (!obj.contains(key)) return fallback;
    try {
        return obj.at(key).get<T>();
    } catch (const json::exception&) {
        throw ConfigError(where + "." + key + " has the wrong type");
    }
}

milliseconds positiveMs(const json& obj, const char* key, milliseconds fallback,
                        const std::string& where) {
    auto value = getOr<long long>(obj, key, fallback.count(), where);
    if (value <= 0) throw ConfigError(where + "." + key + " must be greater than 0");
    return milliseconds{value};
}

Target parseTarget(const json& t, std::size_t index) {
    const std::string where = "targets[" + std::to_string(index) + "]";
    if (!t.is_object()) throw ConfigError(where + " must be an object");

    Target target;
    target.name = getOr<std::string>(t, "name", "", where);
    target.url = getOr<std::string>(t, "url", "", where);
    if (target.name.empty()) throw ConfigError(where + ".name is required");
    if (target.url.rfind("http://", 0) != 0 && target.url.rfind("https://", 0) != 0)
        throw ConfigError(where + ".url must start with http:// or https://");

    target.interval = positiveMs(t, "interval_ms", target.interval, where);
    target.timeout = positiveMs(t, "timeout_ms", target.timeout, where);
    target.degraded_latency = positiveMs(t, "degraded_latency_ms", target.degraded_latency, where);
    target.expected_status = getOr<int>(t, "expected_status", target.expected_status, where);
    if (target.expected_status < 100 || target.expected_status > 599)
        throw ConfigError(where + ".expected_status must be a valid HTTP status");
    if (target.timeout > target.interval)
        throw ConfigError(where + ".timeout_ms must not exceed interval_ms");

    if (t.contains("retry")) {
        const auto& r = t.at("retry");
        const std::string rwhere = where + ".retry";
        target.retry.max_attempts = getOr<int>(r, "max_attempts", target.retry.max_attempts, rwhere);
        if (target.retry.max_attempts < 1 || target.retry.max_attempts > 10)
            throw ConfigError(rwhere + ".max_attempts must be between 1 and 10");
        target.retry.base_delay = positiveMs(r, "base_delay_ms", target.retry.base_delay, rwhere);
        target.retry.max_delay = positiveMs(r, "max_delay_ms", target.retry.max_delay, rwhere);
        if (target.retry.max_delay < target.retry.base_delay)
            throw ConfigError(rwhere + ".max_delay_ms must be >= base_delay_ms");
    }
    return target;
}

}  // namespace

Config parseConfig(const std::string& json_text) {
    json doc;
    try {
        doc = json::parse(json_text);
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("invalid JSON: ") + e.what());
    }
    if (!doc.is_object()) throw ConfigError("config root must be an object");

    Config config;
    auto port = getOr<int>(doc, "metrics_port", config.metrics_port, "config");
    if (port < 0 || port > std::numeric_limits<std::uint16_t>::max())
        throw ConfigError("config.metrics_port must be between 0 and 65535");
    config.metrics_port = static_cast<std::uint16_t>(port);

    auto workers = getOr<int>(doc, "workers", static_cast<int>(config.workers), "config");
    if (workers < 1 || workers > 256) throw ConfigError("config.workers must be between 1 and 256");
    config.workers = static_cast<std::size_t>(workers);

    config.failure_threshold = getOr<int>(doc, "failure_threshold", config.failure_threshold, "config");
    if (config.failure_threshold < 1) throw ConfigError("config.failure_threshold must be >= 1");

    if (!doc.contains("targets") || !doc.at("targets").is_array() || doc.at("targets").empty())
        throw ConfigError("config.targets must be a non-empty array");

    std::set<std::string> names;
    const auto& targets = doc.at("targets");
    for (std::size_t i = 0; i < targets.size(); ++i) {
        Target target = parseTarget(targets[i], i);
        if (!names.insert(target.name).second)
            throw ConfigError("duplicate target name: " + target.name);
        config.targets.push_back(std::move(target));
    }
    return config;
}

Config loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open config file: " + path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseConfig(buffer.str());
}

}  // namespace healthmon
