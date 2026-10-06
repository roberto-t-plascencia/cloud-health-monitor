#pragma once

#include <chrono>
#include <string>

#include "healthmon/config.hpp"

namespace healthmon {

struct ProbeResult {
    bool ok = false;
    int status_code = 0;                 // 0 when no HTTP response was received
    std::chrono::milliseconds latency{0};
    std::string error;                   // empty on success
};

// Interface so the scheduler can be tested with a fake instead of real HTTP.
class Prober {
public:
    virtual ~Prober() = default;
    virtual ProbeResult probe(const Target& target) = 0;
};

// HTTP(S) prober built on libcurl. Thread-safe: each call uses its own easy
// handle. Construct one CurlGlobal in main() before starting any threads.
class CurlProber final : public Prober {
public:
    ProbeResult probe(const Target& target) override;
};

// RAII wrapper for curl_global_init / curl_global_cleanup.
class CurlGlobal {
public:
    CurlGlobal();
    ~CurlGlobal();
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

}  // namespace healthmon
