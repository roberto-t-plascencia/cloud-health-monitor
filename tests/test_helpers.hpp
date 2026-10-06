#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "healthmon/config.hpp"
#include "healthmon/probe.hpp"

namespace healthmon::testing {

inline Target makeTarget(std::string name, std::chrono::milliseconds interval = std::chrono::milliseconds{50}) {
    Target t;
    t.name = std::move(name);
    t.url = "http://example.test/" + t.name;
    t.interval = interval;
    t.timeout = interval;
    t.degraded_latency = std::chrono::milliseconds{500};
    t.retry.max_attempts = 1;
    t.retry.base_delay = std::chrono::milliseconds{1};
    t.retry.max_delay = std::chrono::milliseconds{2};
    return t;
}

inline ProbeResult ok(int latency_ms = 10, int status = 200) {
    return {true, status, std::chrono::milliseconds{latency_ms}, ""};
}

inline ProbeResult fail(std::string error = "connection refused") {
    return {false, 0, std::chrono::milliseconds{0}, std::move(error)};
}

// Prober that returns scripted results, optionally sleeping to simulate a
// slow endpoint, and tracks call counts and maximum concurrency per call.
class FakeProber final : public Prober {
public:
    explicit FakeProber(bool succeed = true, std::chrono::milliseconds delay = {})
        : succeed_(succeed), delay_(delay) {}

    ProbeResult probe(const Target&) override {
        int now = ++active_;
        int prev = max_active_.load();
        while (now > prev && !max_active_.compare_exchange_weak(prev, now)) {
        }
        if (delay_.count() > 0) std::this_thread::sleep_for(delay_);
        ++calls_;
        --active_;
        return succeed_.load() ? ok() : fail();
    }

    void setSucceed(bool s) { succeed_ = s; }
    int calls() const { return calls_.load(); }
    int maxActive() const { return max_active_.load(); }

private:
    std::atomic<bool> succeed_;
    std::chrono::milliseconds delay_;
    std::atomic<int> calls_{0};
    std::atomic<int> active_{0};
    std::atomic<int> max_active_{0};
};

}  // namespace healthmon::testing
