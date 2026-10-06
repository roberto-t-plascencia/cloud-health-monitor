// cloud-health-monitor: checks HTTP(S) endpoints on a schedule, tracks
// latency percentiles and uptime, and exposes Prometheus metrics.

#include <pthread.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "healthmon/config.hpp"
#include "healthmon/http_server.hpp"
#include "healthmon/logger.hpp"
#include "healthmon/metrics.hpp"
#include "healthmon/probe.hpp"
#include "healthmon/prometheus.hpp"
#include "healthmon/scheduler.hpp"
#include "healthmon/thread_pool.hpp"

namespace {

constexpr const char* kVersion = "0.1.0";

struct Options {
    std::string config_path = "config/targets.json";
    bool once = false;
    int port_override = -1;
};

void printUsage() {
    std::cout << "Usage: healthmon [--config PATH] [--port N] [--once]\n"
                 "\n"
                 "  --config PATH  JSON config file (default: config/targets.json,\n"
                 "                 or the HEALTHMON_CONFIG environment variable)\n"
                 "  --port N       Override metrics_port from the config\n"
                 "  --once         Check every target once, print a summary, and exit\n"
                 "                 (exit code 1 if any target failed). Useful in CI.\n"
                 "  --version      Print the version\n"
                 "\n"
                 "Endpoints: /metrics (Prometheus), /status (JSON), /healthz, /readyz\n";
}

bool parseArgs(int argc, char** argv, Options& opts) {
    if (const char* env = std::getenv("HEALTHMON_CONFIG")) opts.config_path = env;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else if (arg == "--version") {
            std::cout << "healthmon " << kVersion << '\n';
            std::exit(0);
        } else if (arg == "--once") {
            opts.once = true;
        } else if (arg == "--config" && i + 1 < argc) {
            opts.config_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port_override = std::atoi(argv[++i]);
        } else {
            std::cerr << "unknown or incomplete argument: " << arg << "\n\n";
            printUsage();
            return false;
        }
    }
    return true;
}

void printSummary(const std::vector<healthmon::TargetSnapshot>& snapshots) {
    std::printf("\n%-20s %-9s %6s %9s  %s\n", "TARGET", "STATE", "STATUS", "LATENCY", "DETAIL");
    for (const auto& s : snapshots) {
        std::printf("%-20s %-9s %6d %7lldms  %s\n", s.name.c_str(),
                    std::string(healthmon::toString(s.state)).c_str(), s.last_status_code,
                    static_cast<long long>(s.last_latency.count()),
                    s.last_error.empty() ? s.url.c_str() : s.last_error.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    using namespace healthmon;

    Options opts;
    if (!parseArgs(argc, argv, opts)) return 2;

    Config config;
    try {
        config = loadConfig(opts.config_path);
    } catch (const ConfigError& e) {
        logEvent("error", "config_invalid", {{"path", opts.config_path}, {"error", e.what()}});
        return 2;
    }
    if (opts.port_override >= 0) config.metrics_port = static_cast<std::uint16_t>(opts.port_override);

    // Block SIGINT/SIGTERM in every thread; main() waits for them with
    // sigwait() below. This is the safe way to handle signals in a
    // multithreaded program (no work inside an async signal handler).
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    CurlGlobal curl_global;
    CurlProber prober;
    MetricsRegistry registry(config.targets, config.failure_threshold);
    ThreadPool pool(config.workers, config.targets.size() + 1);
    Scheduler scheduler(config, prober, registry, pool);

    if (opts.once) {
        scheduler.runOnce();
        auto snapshots = registry.snapshot();
        printSummary(snapshots);
        for (const auto& s : snapshots)
            if (s.consecutive_failures > 0) return 1;
        return 0;
    }

    HttpServer server(config.metrics_port, [&registry](const std::string& path) -> HttpResponse {
        if (path == "/metrics")
            return {200, "text/plain; version=0.0.4; charset=utf-8",
                    renderPrometheus(registry.snapshot())};
        if (path == "/status")
            return {200, "application/json", renderStatusJson(registry.snapshot()) + "\n"};
        if (path == "/healthz") return {200, "text/plain; charset=utf-8", "ok\n"};
        if (path == "/readyz")
            return registry.allChecked()
                       ? HttpResponse{200, "text/plain; charset=utf-8", "ready\n"}
                       : HttpResponse{503, "text/plain; charset=utf-8", "warming up\n"};
        return {404, "text/plain; charset=utf-8", "not found\n"};
    });

    try {
        server.start();
    } catch (const std::exception& e) {
        logEvent("error", "server_start_failed", {{"port", config.metrics_port}, {"error", e.what()}});
        return 1;
    }

    scheduler.start();
    logEvent("info", "started",
             {{"version", kVersion},
              {"targets", config.targets.size()},
              {"workers", config.workers},
              {"metrics_port", server.port()}});

    int sig = 0;
    sigwait(&signals, &sig);
    logEvent("info", "shutting_down", {{"signal", sig == SIGTERM ? "SIGTERM" : "SIGINT"}});

    scheduler.stop();
    pool.shutdown();
    server.stop();
    logEvent("info", "stopped");
    return 0;
}
