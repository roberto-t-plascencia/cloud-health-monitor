#pragma once

#include <string>
#include <vector>

#include "healthmon/metrics.hpp"

namespace healthmon {

// Renders snapshots in the Prometheus text exposition format (version 0.0.4)
// so the monitor can be scraped by Prometheus, Grafana Agent, or Azure
// Monitor managed Prometheus.
std::string renderPrometheus(const std::vector<TargetSnapshot>& snapshots);

// Renders snapshots as a JSON document for the /status endpoint.
std::string renderStatusJson(const std::vector<TargetSnapshot>& snapshots);

// Escapes a Prometheus label value (backslash, double quote, newline).
std::string escapeLabelValue(const std::string& value);

}  // namespace healthmon
