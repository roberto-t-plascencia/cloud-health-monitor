#include "healthmon/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>

namespace healthmon {
namespace {

std::mutex g_log_mutex;
std::atomic<bool> g_enabled{true};

std::string utcTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03dZ", buf, static_cast<int>(ms.count()));
    return out;
}

}  // namespace

void setLoggingEnabled(bool enabled) { g_enabled = enabled; }

void logEvent(std::string_view level, std::string_view event, nlohmann::json fields) {
    if (!g_enabled) return;
    nlohmann::json line = {{"ts", utcTimestamp()}, {"level", level}, {"event", event}};
    if (fields.is_object()) line.update(fields);
    std::string text = line.dump();  // format outside the lock
    std::lock_guard lock(g_log_mutex);
    std::cout << text << '\n' << std::flush;
}

}  // namespace healthmon
