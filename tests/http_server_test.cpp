#include "healthmon/http_server.hpp"

#include <curl/curl.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace healthmon;

namespace {

size_t collect(char* data, size_t size, size_t nmemb, void* out) {
    static_cast<std::string*>(out)->append(data, size * nmemb);
    return size * nmemb;
}

struct Reply {
    long status = 0;
    std::string body;
    std::string content_type;
};

Reply request(std::uint16_t port, const std::string& path, bool post = false) {
    Reply reply;
    CURL* h = curl_easy_init();
    std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &reply.body);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (post) curl_easy_setopt(h, CURLOPT_POSTFIELDS, "x=1");
    if (curl_easy_perform(h) == CURLE_OK) {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &reply.status);
        char* ct = nullptr;
        curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) reply.content_type = ct;
    }
    curl_easy_cleanup(h);
    return reply;
}

class HttpServerTest : public ::testing::Test {
protected:
    HttpServer server{0, [](const std::string& path) -> HttpResponse {
                          if (path == "/metrics") return {200, "text/plain; version=0.0.4", "up 1\n"};
                          if (path == "/boom") throw std::runtime_error("handler failed");
                          return {404, "text/plain", "not found\n"};
                      }};
    void SetUp() override { server.start(); }
};

}  // namespace

TEST(ParseRequestPath, ExtractsPathAndMethod) {
    std::string method;
    EXPECT_EQ(parseRequestPath("GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n", &method), "/metrics");
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(parseRequestPath("GET /status?pretty=1 HTTP/1.0\r\n\r\n"), "/status");
}

TEST(ParseRequestPath, RejectsMalformedRequests) {
    EXPECT_EQ(parseRequestPath(""), "");
    EXPECT_EQ(parseRequestPath("GET\r\n"), "");
    EXPECT_EQ(parseRequestPath("GET metrics HTTP/1.1\r\n"), "");
    EXPECT_EQ(parseRequestPath("GET /metrics SPDY\r\n"), "");
}

TEST_F(HttpServerTest, BindsEphemeralPort) { EXPECT_NE(server.port(), 0); }

TEST_F(HttpServerTest, ServesHandlerResponse) {
    auto r = request(server.port(), "/metrics");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "up 1\n");
    EXPECT_EQ(r.content_type, "text/plain; version=0.0.4");
}

TEST_F(HttpServerTest, UnknownPathIs404) { EXPECT_EQ(request(server.port(), "/nope").status, 404); }

TEST_F(HttpServerTest, NonGetIs405) { EXPECT_EQ(request(server.port(), "/metrics", true).status, 405); }

TEST_F(HttpServerTest, HandlerExceptionIs500) { EXPECT_EQ(request(server.port(), "/boom").status, 500); }

TEST_F(HttpServerTest, ServesManySequentialRequests) {
    for (int i = 0; i < 25; ++i) ASSERT_EQ(request(server.port(), "/metrics").status, 200);
}

TEST_F(HttpServerTest, StopIsFast) {
    auto start = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
    EXPECT_EQ(request(server.port(), "/metrics").status, 0);  // connection refused
}

TEST(HttpServer, PortInUseThrows) {
    HttpServer first(0, [](const std::string&) { return HttpResponse{}; });
    first.start();
    HttpServer second(first.port(), [](const std::string&) { return HttpResponse{}; });
    EXPECT_THROW(second.start(), std::system_error);
}
