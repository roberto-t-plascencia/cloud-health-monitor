#include "healthmon/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <string_view>
#include <system_error>

namespace healthmon {
namespace {

constexpr std::size_t kMaxRequestBytes = 8192;

std::string_view reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 503: return "Service Unavailable";
        default: return "Internal Server Error";
    }
}

void sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;  // client went away; nothing useful to do
        sent += static_cast<std::size_t>(n);
    }
}

std::system_error socketError(const char* what) {
    return std::system_error(errno, std::generic_category(), what);
}

}  // namespace

std::string parseRequestPath(const std::string& request, std::string* method) {
    auto line_end = request.find("\r\n");
    std::string_view line(request.data(), line_end == std::string::npos ? request.size() : line_end);

    auto first_space = line.find(' ');
    if (first_space == std::string_view::npos) return {};
    auto second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) return {};
    if (line.substr(second_space + 1).rfind("HTTP/", 0) != 0) return {};

    if (method) *method = std::string(line.substr(0, first_space));
    std::string_view target = line.substr(first_space + 1, second_space - first_space - 1);
    if (target.empty() || target.front() != '/') return {};
    if (auto q = target.find('?'); q != std::string_view::npos) target = target.substr(0, q);
    return std::string(target);
}

HttpServer::HttpServer(std::uint16_t port, Handler handler)
    : requested_port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) throw socketError("socket");

    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(requested_port_);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        auto err = socketError("bind");
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw err;
    }
    if (::listen(listen_fd_, 16) < 0) {
        auto err = socketError("listen");
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw err;
    }

    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    bound_port_ = ntohs(addr.sin_port);

    thread_ = std::jthread([this](std::stop_token st) { serve(st); });
}

void HttpServer::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();  // serve() polls with a timeout, so it notices quickly
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::serve(std::stop_token stop) {
    while (!stop.stop_requested()) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        int ready = ::poll(&pfd, 1, 200);  // wake up regularly to check for stop
        if (ready <= 0) continue;

        int client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;
        handleConnection(client);
        ::close(client);
    }
}

void HttpServer::handleConnection(int client_fd) {
    timeval tv{2, 0};  // don't let a slow client stall the server
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string request;
    char buf[1024];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxRequestBytes) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        request.append(buf, static_cast<std::size_t>(n));
    }

    std::string method;
    std::string path = parseRequestPath(request, &method);
    HttpResponse response;
    if (path.empty()) {
        response = {400, "text/plain; charset=utf-8", "bad request\n"};
    } else if (method != "GET") {
        response = {405, "text/plain; charset=utf-8", "method not allowed\n"};
    } else {
        try {
            response = handler_(path);
        } catch (const std::exception& e) {
            response = {500, "text/plain; charset=utf-8", std::string("error: ") + e.what() + "\n"};
        }
    }

    std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " +
                      std::string(reasonPhrase(response.status)) + "\r\n" +
                      "Content-Type: " + response.content_type + "\r\n" +
                      "Content-Length: " + std::to_string(response.body.size()) + "\r\n" +
                      "Connection: close\r\n\r\n" + response.body;
    sendAll(client_fd, out);
}

}  // namespace healthmon
