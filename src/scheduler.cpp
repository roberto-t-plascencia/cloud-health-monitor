#include "healthmon/scheduler.hpp"

#include <random>

#include "healthmon/logger.hpp"
#include "healthmon/retry.hpp"

namespace healthmon {
namespace {

std::mt19937& threadRng() {
    thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

}  // namespace

Scheduler::Scheduler(const Config& config, Prober& prober, MetricsRegistry& registry,
                     ThreadPool& pool)
    : config_(config),
      prober_(prober),
      registry_(registry),
      pool_(pool),
      in_flight_(config.targets.size(), false) {}

Scheduler::~Scheduler() { stop(); }

void Scheduler::start() {
    std::lock_guard lock(mutex_);
    if (thread_.joinable()) return;
    stopping_ = false;
    const auto now = Clock::now();
    const auto n = config_.targets.size();
    for (std::size_t i = 0; i < n; ++i) {
        // Stagger: target i starts i/n of the way through its own interval.
        auto offset = config_.targets[i].interval * static_cast<long long>(i) /
                      static_cast<long long>(n);
        heap_.push({now + offset, i});
    }
    thread_ = std::thread([this] { loop(); });
}

void Scheduler::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();

    // Wait for checks already handed to the pool; their retry sleeps were
    // interrupted above, so this is bounded by one probe timeout.
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return pending_ == 0; });
    heap_ = {};
}

std::size_t Scheduler::skippedTicks() const {
    std::lock_guard lock(mutex_);
    return skipped_;
}

void Scheduler::loop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        if (heap_.empty()) {
            cv_.wait(lock, [this] { return stopping_ || !heap_.empty(); });
            continue;
        }
        const Due next = heap_.top();
        if (cv_.wait_until(lock, next.when, [this] { return stopping_; })) break;
        if (Clock::now() < next.when) continue;  // woke early; re-check the heap
        heap_.pop();

        const auto& target = config_.targets[next.index];
        auto following = next.when + target.interval;
        if (following < Clock::now()) following = Clock::now() + target.interval;  // fell behind
        heap_.push({following, next.index});

        if (in_flight_[next.index]) {
            ++skipped_;
            lock.unlock();
            logEvent("warn", "check_skipped",
                     {{"target", target.name}, {"reason", "previous check still running"}});
            lock.lock();
            continue;
        }
        in_flight_[next.index] = true;
        ++pending_;
        lock.unlock();  // never hold our lock while calling into the pool
        bool submitted = pool_.submit([this, i = next.index] { runCheck(i); });
        lock.lock();
        if (!submitted) {
            in_flight_[next.index] = false;
            --pending_;
            cv_.notify_all();
        }
    }
}

bool Scheduler::dispatch(std::size_t index) {
    {
        std::lock_guard lock(mutex_);
        in_flight_[index] = true;
        ++pending_;
    }
    if (pool_.submit([this, index] { runCheck(index); })) return true;
    std::lock_guard lock(mutex_);
    in_flight_[index] = false;
    --pending_;
    cv_.notify_all();
    return false;
}

void Scheduler::runOnce() {
    for (std::size_t i = 0; i < config_.targets.size(); ++i) dispatch(i);
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return pending_ == 0; });
}

void Scheduler::interruptibleSleep(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, duration, [this] { return stopping_; });
}

void Scheduler::runCheck(std::size_t index) {
    const Target& target = config_.targets[index];

    RetryOutcome outcome = runWithRetry(
        target.retry, [&] { return prober_.probe(target); },
        [this](std::chrono::milliseconds d) { interruptibleSleep(d); }, threadRng(),
        [this] {
            std::lock_guard lock(mutex_);
            return stopping_;
        });

    auto change = registry_.record(target.name, outcome.result, outcome.attempts);

    if (!outcome.result.ok) {
        logEvent("warn", "check_failed",
                 {{"target", target.name},
                  {"attempts", outcome.attempts},
                  {"status", outcome.result.status_code},
                  {"latency_ms", outcome.result.latency.count()},
                  {"error", outcome.result.error}});
    }
    if (change) {
        logEvent(change->to == HealthState::Down ? "error" : "info", "state_change",
                 {{"target", change->target},
                  {"from", std::string(toString(change->from))},
                  {"to", std::string(toString(change->to))}});
    }

    // Notify while still holding the lock: once pending_ hits 0, stop() or
    // runOnce() may return and the Scheduler can be destroyed, so this thread
    // must not touch cv_ after releasing the mutex. (ThreadSanitizer caught
    // the original notify-after-unlock version of this.)
    std::lock_guard lock(mutex_);
    in_flight_[index] = false;
    --pending_;
    cv_.notify_all();
}

}  // namespace healthmon
