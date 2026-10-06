#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace healthmon {

// Structured JSON-lines logging to stdout, one object per line, e.g.
//   {"ts":"2026-10-06T17:00:00.123Z","level":"warn","event":"state_change",...}
// Log collectors (Fluent Bit, Azure Monitor, CloudWatch) can parse these
// without custom regexes. Thread-safe.
void logEvent(std::string_view level, std::string_view event, nlohmann::json fields = {});

// Silences logging (used by unit tests).
void setLoggingEnabled(bool enabled);

}  // namespace healthmon
