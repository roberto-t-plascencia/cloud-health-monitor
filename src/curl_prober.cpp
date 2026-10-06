#include <curl/curl.h>

#include <memory>
#include <stdexcept>

#include "healthmon/probe.hpp"

namespace healthmon {
namespace {

// Discard the response body: we only care about status and timing.
size_t discardBody(char*, size_t size, size_t nmemb, void*) { return size * nmemb; }

struct EasyDeleter {
    void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

}  // namespace

CurlGlobal::CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}

CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

ProbeResult CurlProber::probe(const Target& target) {
    ProbeResult result;
    std::unique_ptr<CURL, EasyDeleter> curl(curl_easy_init());
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    char error_buffer[CURL_ERROR_SIZE] = {};
    CURL* h = curl.get();
    curl_easy_setopt(h, CURLOPT_URL, target.url.c_str());
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(target.timeout.count()));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(target.timeout.count()));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);           // required for multithreaded use
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "cloud-health-monitor/0.1");
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discardBody);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, error_buffer);

    CURLcode code = curl_easy_perform(h);

    curl_off_t total_us = 0;
    curl_easy_getinfo(h, CURLINFO_TOTAL_TIME_T, &total_us);
    result.latency = std::chrono::milliseconds{total_us / 1000};

    if (code != CURLE_OK) {
        result.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
        return result;
    }

    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    result.status_code = static_cast<int>(status);
    result.ok = result.status_code == target.expected_status;
    if (!result.ok)
        result.error = "unexpected status " + std::to_string(status) + " (expected " +
                       std::to_string(target.expected_status) + ")";
    return result;
}

}  // namespace healthmon
