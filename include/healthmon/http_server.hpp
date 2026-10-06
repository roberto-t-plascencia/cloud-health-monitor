#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace healthmon {

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

// Small blocking HTTP/1.1 server for the /metrics, /healthz and /status
// endpoints. It handles one connection at a time on a background thread,
// which is plenty for a Prometheus scrape every few seconds. GET only.
class HttpServer {
public:
    using Handler = std::function<HttpResponse(const std::string& path)>;

    // port 0 asks the OS for a free port (used by tests); see port().
    HttpServer(std::uint16_t port, Handler handler);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds and starts serving. Throws std::system_error if the port is taken.
    void start();
    void stop();

    std::uint16_t port() const { return bound_port_; }

private:
    void serve(std::stop_token stop);
    void handleConnection(int client_fd);

    std::uint16_t requested_port_;
    std::uint16_t bound_port_ = 0;
    Handler handler_;
    int listen_fd_ = -1;
    std::jthread thread_;
};

// Exposed for unit tests: extracts the path from "GET /path?query HTTP/1.1".
// Returns an empty string if the request line is malformed.
std::string parseRequestPath(const std::string& request, std::string* method = nullptr);

}  // namespace healthmon
