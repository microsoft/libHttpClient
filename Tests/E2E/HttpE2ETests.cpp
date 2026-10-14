// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// HTTP end-to-end tests: the platform HTTP provider (WinHTTP on Win32, curl on Linux) against the
// loopback server. Where the providers legitimately differ, the expectation is spelled out per
// provider (E2E_HTTP_WINHTTP / E2E_HTTP_CURL); the WinHTTP expectations are unchanged.

#include "E2EHelpers.h"

#include <algorithm>
#include <memory>

using namespace e2e;

namespace
{

std::string EchoField(const std::string& json, const char* key)
{
    return JsonFieldOr(json, key);
}

// A port nothing is listening on: bind an ephemeral port, then release it.
uint16_t UnusedLoopbackPort()
{
    uint16_t port = platform::UnusedLoopbackPort();
    E2E_CHECK_MSG(port != 0, "could not reserve an ephemeral port");
    return port;
}

#if defined(E2E_HTTP_CURL)
// The curl provider doesn't set CURLOPT_FOLLOWLOCATION, so a redirect is returned to the caller
// as-is (WinHTTP and NSURLSession follow redirects). Check the 3xx and its Location header are
// passed through intact. If the curl provider starts following redirects, drop this branch.
void CheckRedirectPassedThrough(Call& call, HRESULT hr, const std::string& expectedLocation)
{
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 302, "expected the 302 to be returned unfollowed: hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string location;
    E2E_CHECK_MSG(call.ResponseHeader("Location", location), "302 response has no Location header");
    E2E_CHECK_EQ(expectedLocation, location);
    Info("curl provider returned the 302 to the caller (Location: %s) instead of following it", location.c_str());
}
#endif
// Collects every chunk handed to a custom response body write function.
struct BodyCollector
{
    std::mutex mutex;
    std::vector<uint8_t> data;
    size_t calls{ 0 };
    size_t largestChunk{ 0 };

    static HRESULT CALLBACK Write(HCCallHandle, const uint8_t* source, size_t bytesAvailable, void* context)
    {
        auto* self = static_cast<BodyCollector*>(context);
        std::lock_guard<std::mutex> lock{ self->mutex };
        self->data.insert(self->data.end(), source, source + bytesAvailable);
        ++self->calls;
        self->largestChunk = std::max(self->largestChunk, bytesAvailable);
        return S_OK;
    }
};

struct ProgressRecorder
{
    std::mutex mutex;
    std::vector<std::pair<uint64_t, uint64_t>> reports;

    static HRESULT CALLBACK Report(HCCallHandle, uint64_t current, uint64_t total, void* context)
    {
        auto* self = static_cast<ProgressRecorder*>(context);
        std::lock_guard<std::mutex> lock{ self->mutex };
        self->reports.emplace_back(current, total);
        return S_OK;
    }

    void Verify(const char* direction, uint64_t expectedTotal, size_t minimumReports)
    {
        std::lock_guard<std::mutex> lock{ mutex };
        E2E_CHECK_MSG(reports.size() >= minimumReports, "%zu %s progress callbacks, expected at least %zu", reports.size(), direction, minimumReports);
        uint64_t previous = 0;
        for (const auto& r : reports)
        {
            E2E_CHECK_MSG(r.first >= previous, "%s progress went backwards (%llu after %llu)", direction, r.first, previous);
            E2E_CHECK_MSG(r.second == expectedTotal, "%s progress total %llu, expected %llu", direction, r.second, expectedTotal);
            E2E_CHECK_MSG(r.first <= r.second, "%s progress current %llu > total %llu", direction, r.first, r.second);
            previous = r.first;
        }
        // The first report must not claim the whole body (WinHTTP upload progress used to report
        // the response counter, i.e. always 100%). WinHTTP first reports after a chunk has moved;
        // curl's transfer-info callback first fires before any bytes move, so 0 is valid there
        // (and for other providers, whose first-report timing isn't pinned down).
#if defined(E2E_HTTP_WINHTTP)
        E2E_CHECK_MSG(reports.front().first > 0 && reports.front().first < expectedTotal, "first %s progress report was %llu of %llu", direction, reports.front().first, expectedTotal);
#else
        E2E_CHECK_MSG(reports.front().first < expectedTotal, "first %s progress report was %llu of %llu", direction, reports.front().first, expectedTotal);
#endif
        std::string trail;
        for (const auto& r : reports)
        {
            trail += std::to_string(r.first) + " ";
        }
        Info("%s progress: %zu callbacks: %s", direction, reports.size(), trail.c_str());
    }
};

} // namespace

// ------------------------------------------------------------------------------------------------
// Methods, headers, bodies
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_methods_roundtrip)
{
    for (const char* method : { "GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS" })
    {
        bool hasBody = strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0;
        std::string body = std::string{ "body for " } + method;
        Call call;
        call.Url(method, HttpUrl("/echo?m=") + method).NoRetry();
        if (hasBody)
        {
            call.Body(body);
        }
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "%s: hr=%s %s", method, HrToString(hr).c_str(), call.Describe().c_str());
        std::string json = call.ResponseString();
        E2E_CHECK_EQ(std::string{ method }, EchoField(json, "method"));
        E2E_CHECK_EQ(hasBody ? body : std::string{}, EchoField(json, "body"));
    }

    // HEAD: same headers as GET, no body.
    Call head;
    head.Url("HEAD", HttpUrl("/echo")).NoRetry();
    HRESULT hr = head.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && head.Status() == 200, "HEAD: hr=%s %s", HrToString(hr).c_str(), head.Describe().c_str());
    E2E_CHECK_EQ(size_t{ 0 }, head.ResponseBytes().size());
    std::string echoHeader;
    E2E_CHECK_MSG(head.ResponseHeader("X-Lhc-Echo", echoHeader), "HEAD response is missing X-Lhc-Echo");
}

E2E_TEST(http_request_and_response_headers)
{
    // UTF-8 "héllo": every code point is in ISO-8859-1.
    const char* latin1RangeValue = "h\xC3\xA9llo";

    Call call;
    call.Url("GET", HttpUrl("/echo")).NoRetry()
        .Header("X-Lhc-Ascii", "hello world")
        .Header("X-Lhc-Latin1", latin1RangeValue)
        .Header("X-Lhc-Tab", "a\tb");
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    E2E_CHECK_EQ(std::string{ "hello world" }, EchoField(json, "x-lhc-ascii"));
    E2E_CHECK_EQ(std::string{ "a\tb" }, EchoField(json, "x-lhc-tab"));
    // The server decodes the value as UTF-8 if valid, else ISO-8859-1, so this checks the
    // characters arrived. WinHTTP actually puts ISO-8859-1 bytes on the wire (68 e9 6c 6c 6f).
    E2E_CHECK_EQ(std::string{ latin1RangeValue }, EchoField(json, "x-lhc-latin1"));
    size_t rawPos = json.find("\"non_ascii_header_bytes\"");
    E2E_CHECK(rawPos != std::string::npos);
    Info("X-Lhc-Latin1 wire bytes: %s", JsonFieldOr(json.substr(rawPos), "x-lhc-latin1").c_str());

    // Response headers: lookup is case-insensitive and enumerable.
    std::string value;
    E2E_CHECK(call.ResponseHeader("X-Lhc-Echo", value));
    E2E_CHECK_EQ(std::string{ "1" }, value);
    E2E_CHECK(call.ResponseHeader("x-lhc-echo", value));
    E2E_CHECK(call.ResponseHeader("X-Lhc-Multi", value));
    E2E_CHECK_MSG(value.find('a') != std::string::npos && value.find('b') != std::string::npos, "repeated header X-Lhc-Multi = '%s'", value.c_str());
    Info("repeated response header X-Lhc-Multi surfaced as '%s'", value.c_str());

    uint32_t count = 0;
    E2E_CHECK_HR(HCHttpCallResponseGetNumHeaders(call, &count));
    bool found = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = nullptr;
        const char* v = nullptr;
        E2E_CHECK_HR(HCHttpCallResponseGetHeaderAtIndex(call, i, &name, &v));
        if (name && platform::StrICmp(name, "X-Lhc-Echo") == 0)
        {
            found = true;
        }
    }
    E2E_CHECK_MSG(found, "X-Lhc-Echo not found among %u enumerated headers", count);
}

E2E_TEST(http_request_header_outside_latin1)
{
    // WinHTTP can only send header values in ISO-8859-1, so a UTF-8 value containing a code point
    // above U+00FF ("✓" U+2713) can't be sent. Today the whole request fails with E_INVALIDARG
    // from WinHttpAddRequestHeaders. Record it; the requirement here is a clean, prompt failure
    // (no crash or hang) or a successful send.
    Call call;
    call.Url("GET", HttpUrl("/echo")).NoRetry().Header("X-Lhc-Unicode", "check \xE2\x9C\x93");
    HRESULT hr = call.Perform();
    Info("hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    if (SUCCEEDED(hr) && call.Status() == 200)
    {
        Info("server received X-Lhc-Unicode = '%s'", EchoField(call.ResponseString(), "x-lhc-unicode").c_str());
    }
    else
    {
        E2E_CHECK_EQ(E_INVALIDARG, hr);
    }
}

E2E_TEST(http_request_body_sizes)
{
    for (size_t size : { size_t{ 0 }, size_t{ 1024 }, size_t{ 1024 * 1024 } })
    {
        auto body = PatternBytes(0, size);
        Call call;
        call.Url("POST", HttpUrl("/echo")).NoRetry();
        if (size > 0)
        {
            call.Body(body);
        }
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "size %zu: hr=%s %s", size, HrToString(hr).c_str(), call.Describe().c_str());
        std::string json = call.ResponseString();
        E2E_CHECK_EQ(static_cast<uint64_t>(size), JsonUInt(json, "body_length"));
        E2E_CHECK_EQ(static_cast<uint64_t>(Crc32(body.data(), body.size())), JsonUInt(json, "body_crc32"));
    }
}

E2E_TEST(http_request_body_read_function)
{
    struct Source
    {
        size_t size;
        static HRESULT CALLBACK Read(HCCallHandle, size_t offset, size_t bytesAvailable, void* context, uint8_t* destination, size_t* bytesWritten)
        {
            auto* self = static_cast<Source*>(context);
            size_t n = std::min(bytesAvailable, self->size - std::min(offset, self->size));
            auto bytes = PatternBytes(offset, n);
            memcpy(destination, bytes.data(), n);
            *bytesWritten = n;
            return S_OK;
        }
    } source{ 300 * 1000 };

    Call call;
    call.Url("PUT", HttpUrl("/echo")).NoRetry();
    E2E_CHECK_HR(HCHttpCallRequestSetRequestBodyReadFunction(call, Source::Read, source.size, &source));
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    auto expected = PatternBytes(0, source.size);
    E2E_CHECK_EQ(static_cast<uint64_t>(source.size), JsonUInt(json, "body_length"));
    E2E_CHECK_EQ(static_cast<uint64_t>(Crc32(expected.data(), expected.size())), JsonUInt(json, "body_crc32"));
}

// ------------------------------------------------------------------------------------------------
// Response bodies and receive buffer sizes (replaces the internet-based
// Tests/UnitTests/Tests/BufferSize E2E/LargeFile/MegaFile/HeaderAnalysis scenarios)
// ------------------------------------------------------------------------------------------------

namespace
{

void DownloadAndVerify(const std::string& path, size_t expectedSize, size_t bufferSize, bool expectContentLength)
{
    Call call;
    call.Url("GET", HttpUrl(path)).NoRetry();
    if (bufferSize > 0)
    {
        E2E_CHECK_HR(HCHttpCallRequestSetMaxReceiveBufferSize(call, bufferSize));
        size_t readBack = 0;
        E2E_CHECK_HR(HCHttpCallRequestGetMaxReceiveBufferSize(call, &readBack));
        E2E_CHECK_EQ(bufferSize, readBack);
    }

    auto start = std::chrono::steady_clock::now();
    HRESULT hr = call.Perform(std::chrono::seconds{ 120 });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "%s buffer=%zu: hr=%s %s", path.c_str(), bufferSize, HrToString(hr).c_str(), call.Describe().c_str());

    auto body = call.ResponseBytes();
    E2E_CHECK_MSG(body.size() == expectedSize, "%s buffer=%zu: got %zu bytes, expected %zu", path.c_str(), bufferSize, body.size(), expectedSize);
    E2E_CHECK_MSG(IsPattern(body.data(), body.size()), "%s buffer=%zu: body content is corrupted", path.c_str(), bufferSize);

    std::string contentLength;
    bool hasContentLength = call.ResponseHeader("Content-Length", contentLength);
    E2E_CHECK_MSG(hasContentLength == expectContentLength, "%s: Content-Length header present=%d, expected %d", path.c_str(), hasContentLength, expectContentLength);
    Info("%-22s buffer=%-8zu %8.1f MB/s", path.c_str(), bufferSize, (expectedSize / (1024.0 * 1024.0)) / std::max(seconds, 0.001));
}

} // namespace

E2E_TEST(http_response_sizes_with_receive_buffer)
{
    for (size_t size : { size_t{ 64 * 1024 }, size_t{ 1024 * 1024 }, size_t{ 20 * 1024 * 1024 } })
    {
        for (size_t buffer : { size_t{ 0 }, size_t{ 4 * 1024 }, size_t{ 64 * 1024 }, size_t{ 1024 * 1024 } })
        {
            DownloadAndVerify("/bytes/" + std::to_string(size), size, buffer, true);
        }
    }
}

E2E_TEST(http_chunked_response_with_receive_buffer)
{
    const size_t size = 1024 * 1024 + 123;
    for (size_t buffer : { size_t{ 0 }, size_t{ 4 * 1024 }, size_t{ 256 * 1024 } })
    {
        DownloadAndVerify("/stream/" + std::to_string(size) + "?chunk=7000", size, buffer, false);
    }
}

E2E_TEST(http_response_write_function_honours_buffer_size)
{
    const size_t size = 2 * 1024 * 1024;
    for (size_t buffer : { size_t{ 4 * 1024 }, size_t{ 64 * 1024 } })
    {
        BodyCollector collector;
        Call call;
        call.Url("GET", HttpUrl("/bytes/" + std::to_string(size))).NoRetry();
        E2E_CHECK_HR(HCHttpCallRequestSetMaxReceiveBufferSize(call, buffer));
        E2E_CHECK_HR(HCHttpCallResponseSetResponseBodyWriteFunction(call, BodyCollector::Write, &collector));
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
        E2E_CHECK_EQ(size, collector.data.size());
        E2E_CHECK(IsPattern(collector.data.data(), collector.data.size()));
        // The first read happens before the provider knows the Content-Length, so it may use
        // WinHTTP's available-bytes count; every later chunk is capped by the buffer size.
        Info("buffer=%zu: %zu write callbacks, largest chunk %zu", buffer, collector.calls, collector.largestChunk);
        E2E_CHECK_MSG(collector.calls >= size / std::max(collector.largestChunk, size_t{ 1 }), "fewer callbacks than expected");
    }
}

// ------------------------------------------------------------------------------------------------
// Compression
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_gzip_response_decoded)
{
    Call call;
    call.Url("GET", HttpUrl("/gzip")).NoRetry();
    E2E_CHECK_HR(HCHttpCallResponseSetGzipCompressed(call, true));
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());

    std::string plainLength, plainCrc;
    E2E_CHECK(call.ResponseHeader("X-Lhc-Plain-Length", plainLength));
    E2E_CHECK(call.ResponseHeader("X-Lhc-Plain-Crc32", plainCrc));
    auto body = call.ResponseBytes();
    E2E_CHECK_EQ(strtoull(plainLength.c_str(), nullptr, 10), static_cast<unsigned long long>(body.size()));
    E2E_CHECK_EQ(strtoull(plainCrc.c_str(), nullptr, 10), static_cast<unsigned long long>(Crc32(body.data(), body.size())));
    E2E_CHECK(std::string(body.begin(), body.begin() + 34) == "libHttpClient gzip response line 0");
}

E2E_TEST(http_gzip_request_body)
{
    std::string body;
    for (int i = 0; i < 2000; ++i)
    {
        body += "compressible request body line " + std::to_string(i) + "\n";
    }
    Call call;
    call.Url("POST", HttpUrl("/echo")).NoRetry().Body(body);
    E2E_CHECK_HR(HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Medium));
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    E2E_CHECK_EQ(std::string{ "gzip" }, EchoField(json, "content_encoding"));
    E2E_CHECK_EQ(static_cast<uint64_t>(body.size()), JsonUInt(json, "body_length"));
    E2E_CHECK_EQ(static_cast<uint64_t>(Crc32(reinterpret_cast<const uint8_t*>(body.data()), body.size())), JsonUInt(json, "body_crc32"));
    E2E_CHECK_MSG(JsonUInt(json, "wire_body_length") < body.size() / 4, "request body was not compressed (%llu wire bytes)", JsonUInt(json, "wire_body_length"));
}

// ------------------------------------------------------------------------------------------------
// Progress
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_progress_callbacks)
{
    // The minimum report interval is 1 s (0 is rejected), and the first report fires immediately.
    const size_t size = 1024 * 1024;

    ProgressRecorder upload;
    {
        Call call;
        call.Url("POST", HttpUrl("/echo")).NoRetry().Body(PatternBytes(0, size));
        E2E_CHECK_HR(HCHttpCallRequestSetProgressReportFunction(call, ProgressRecorder::Report, true, 1, &upload));
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "upload: hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    }
    upload.Verify("upload", size, 1);

    // Download dribbled out over ~2.1 s so more than one report is due.
    ProgressRecorder download;
    {
        Call call;
        call.Url("GET", HttpUrl("/bytes/" + std::to_string(size) + "?chunk=131072&delay_ms=300")).NoRetry();
        E2E_CHECK_HR(HCHttpCallRequestSetMaxReceiveBufferSize(call, 64 * 1024));
        E2E_CHECK_HR(HCHttpCallRequestSetProgressReportFunction(call, ProgressRecorder::Report, false, 1, &download));
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "download: hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
        E2E_CHECK_EQ(size, call.ResponseBytes().size());
    }
    download.Verify("download", size, 2);
}

// ------------------------------------------------------------------------------------------------
// Status codes and retry
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_status_passthrough)
{
    for (uint32_t status : { 201u, 204u, 400u, 404u, 500u, 503u })
    {
        Call call;
        call.Url("GET", HttpUrl("/status/" + std::to_string(status))).NoRetry();
        HRESULT hr = call.Perform();
        E2E_CHECK_MSG(SUCCEEDED(hr), "status %u: hr=%s", status, HrToString(hr).c_str());
        E2E_CHECK_EQ(status, call.Status());
        E2E_CHECK_EQ(S_OK, call.NetworkError());
        E2E_CHECK_EQ(1u, [&] { uint32_t n = 0; HCHttpCallGetPerformCount(call, &n); return n; }());
    }
}

E2E_TEST(http_retry_after_503_then_success)
{
    const std::string id = UniqueId("retry");
    Call call;
    call.Url("GET", HttpUrl("/retry-after/2?status=503&after=1&id=" + id));
    E2E_CHECK_HR(HCHttpCallRequestSetRetryAllowed(call, true));
    E2E_CHECK_HR(HCHttpCallRequestSetRetryDelay(call, 1));
    E2E_CHECK_HR(HCHttpCallRequestSetTimeoutWindow(call, 30));

    auto start = std::chrono::steady_clock::now();
    HRESULT hr = call.Perform();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    E2E_CHECK_EQ(uint64_t{ 3 }, JsonUInt(call.ResponseString(), "attempts"));
    // Two Retry-After: 1 waits.
    E2E_CHECK_MSG(seconds >= 1.9, "retries did not honour Retry-After (%.2f s)", seconds);
    Info("succeeded after 3 attempts in %.2f s", seconds);
}

E2E_TEST(http_retry_disabled_returns_first_503)
{
    const std::string id = UniqueId("noretry");
    Call call;
    call.Url("GET", HttpUrl("/retry-after/5?status=503&after=1&id=" + id)).NoRetry();
    HRESULT hr = call.Perform();
    E2E_CHECK_HR(hr);
    E2E_CHECK_EQ(503u, call.Status());
    std::string retryAfter;
    E2E_CHECK(call.ResponseHeader("Retry-After", retryAfter));
    E2E_CHECK_EQ(std::string{ "1" }, retryAfter);
    E2E_CHECK_EQ(std::string{ "attempt 1" }, call.ResponseString());
}

// ------------------------------------------------------------------------------------------------
// Network failures
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_timeout)
{
    // The response would take 20 s; a 1 s timeout must fail the call long before that. (WinHTTP
    // takes a few seconds to report it, so the bound is loose.)
    Call call;
    call.Url("GET", HttpUrl("/delay/20000")).NoRetry();
    E2E_CHECK_HR(HCHttpCallRequestSetTimeout(call, 1));
    auto start = std::chrono::steady_clock::now();
    HRESULT hr = call.Perform();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    Info("hr=%s %s after %.2f s", HrToString(hr).c_str(), call.Describe().c_str(), seconds);
    E2E_CHECK_MSG(FAILED(hr) || FAILED(call.NetworkError()), "a 1 s timeout against a 20 s response did not fail: %s", call.Describe().c_str());
    E2E_CHECK_MSG(seconds < 12.0, "timeout took %.2f s", seconds);
}

E2E_TEST(http_timeout_is_per_call)
{
    // The per-call timeout is applied to that call's WinHTTP request handle (it used to be set on
    // the session shared by all calls). A short timeout on one call must not affect the next.
    {
        Call shortTimeout;
        shortTimeout.Url("GET", HttpUrl("/status/200")).NoRetry();
        E2E_CHECK_HR(HCHttpCallRequestSetTimeout(shortTimeout, 1));
        E2E_CHECK_HR(shortTimeout.Perform());
        E2E_CHECK_EQ(200u, shortTimeout.Status());
    }
    Call defaultTimeout;
    defaultTimeout.Url("GET", HttpUrl("/delay/2500")).NoRetry();
    HRESULT hr = defaultTimeout.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && defaultTimeout.Status() == 200, "a call with the default 30 s timeout failed after another call used 1 s: hr=%s %s", HrToString(hr).c_str(), defaultTimeout.Describe().c_str());
}

E2E_TEST(http_connection_refused)
{
    // TODO: once PR #1031 (platform error message text) is in, also assert
    // HCHttpCallResponseGetPlatformNetworkErrorMessage returns a non-empty message.
    uint16_t port = UnusedLoopbackPort();
    Call call;
    call.Url("GET", Format("http://127.0.0.1:%u/", port)).NoRetry();
    HRESULT hr = call.Perform();
    Info("hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    E2E_CHECK_MSG(FAILED(hr) || FAILED(call.NetworkError()), "connecting to a closed port did not fail: %s", call.Describe().c_str());
    E2E_CHECK_EQ(0u, call.Status());
}

E2E_TEST(http_dns_failure)
{
    // .invalid is reserved (RFC 2606) and never resolves.
    Call call;
    call.Url("GET", "http://lhc-e2e-does-not-exist.invalid/").NoRetry();
    HRESULT hr = call.Perform();
    Info("hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    E2E_CHECK_MSG(FAILED(hr) || FAILED(call.NetworkError()), "an unresolvable host did not fail: %s", call.Describe().c_str());
}

// ------------------------------------------------------------------------------------------------
// Redirects
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_redirect_same_origin_keeps_authorization)
{
    Call call;
    call.Url("GET", HttpUrl("/redirect/same")).NoRetry()
        .Header("Authorization", "Bearer e2e-token")
        .Header("X-Lhc-Custom", "kept");
    HRESULT hr = call.Perform();
#if defined(E2E_HTTP_CURL)
    CheckRedirectPassedThrough(call, hr, HttpUrl("/echo"));
    return;
#endif
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    E2E_CHECK_EQ(static_cast<uint64_t>(Server().http), JsonUInt(json, "port"));
    E2E_CHECK_EQ(std::string{ "/echo" }, EchoField(json, "path"));
    E2E_CHECK_EQ(std::string{ "Bearer e2e-token" }, EchoField(json, "authorization"));
    E2E_CHECK_EQ(std::string{ "kept" }, EchoField(json, "x-lhc-custom"));
}

E2E_TEST(http_redirect_cross_origin)
{
    Call call;
    call.Url("GET", HttpUrl("/redirect/cross")).NoRetry()
        .Header("Authorization", "Bearer e2e-token")
        .Header("X-Lhc-Custom", "kept");
    HRESULT hr = call.Perform();
#if defined(E2E_HTTP_CURL)
    CheckRedirectPassedThrough(call, hr, Http2Url("/echo"));
    return;
#endif
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    E2E_CHECK_EQ(static_cast<uint64_t>(Server().http2), JsonUInt(json, "port"));
    E2E_CHECK_EQ(std::string{ "kept" }, EchoField(json, "x-lhc-custom"));
    // TODO(#1032): once cross-origin redirects strip credentials, assert Authorization is absent
    // here. Until then just record what the provider does.
    Info("Authorization forwarded to the cross-origin redirect target: %s", EchoField(json, "authorization").c_str());
}

// ------------------------------------------------------------------------------------------------
// HTTPS
// ------------------------------------------------------------------------------------------------

E2E_TEST(https_self_signed_rejected_by_default)
{
    RequireHttps();
    Call call;
    call.Url("GET", HttpsUrl("/echo")).NoRetry();
    HRESULT hr = call.Perform();
    Info("hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    E2E_CHECK_MSG(FAILED(hr) || FAILED(call.NetworkError()), "a self-signed certificate was accepted with validation on: %s", call.Describe().c_str());
    E2E_CHECK_EQ(0u, call.Status());
}

E2E_TEST(https_ssl_validation_disabled)
{
    RequireHttps();
#if HC_PLATFORM_IS_MICROSOFT
    Call call;
    call.Url("POST", HttpsUrl("/echo")).NoRetry().Body("over tls");
    E2E_CHECK_HR(HCHttpCallRequestSetSSLValidation(call, false));
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "hr=%s %s", HrToString(hr).c_str(), call.Describe().c_str());
    std::string json = call.ResponseString();
    E2E_CHECK_EQ(std::string{ "https" }, EchoField(json, "server"));
    E2E_CHECK_EQ(std::string{ "over tls" }, EchoField(json, "body"));
#else
    Skip(std::string{ "HCHttpCallRequestSetSSLValidation is only available on Windows (" } + HttpProviderName + " always validates)");
#endif
}

// ------------------------------------------------------------------------------------------------
// Cancellation, cleanup and concurrency
// ------------------------------------------------------------------------------------------------

E2E_TEST(http_cancel_inflight)
{
    // By design (HC_CALL::PerfomAsyncProvider, XAsyncOp::Cancel) cancelling while the provider is
    // running the request does not abort the WinHTTP request: the perform completes when that
    // request finishes. Check that it does complete, with success or E_ABORT, and not before the
    // response arrives.
    Call call;
    call.Url("GET", HttpUrl("/delay/1500")).NoRetry();
    auto* async = new XAsyncBlock{};
    E2E_CHECK_HR(HCHttpCallPerformAsync(call, async));
    platform::SleepMs(300);
    auto start = std::chrono::steady_clock::now();
    XAsyncCancel(async);
    HRESULT hr = WaitAsync(async, std::chrono::seconds{ 15 });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (hr == E_PENDING)
    {
        PoisonProcess("cancelled HTTP call never completed");
        Fail(__FILE__, __LINE__, "the cancelled call did not complete within 15 s");
    }
    delete async;
    Info("cancelled in-flight call completed with hr=%s status=%u after %.2f s", HrToString(hr).c_str(), call.Status(), seconds);
    E2E_CHECK_MSG(hr == S_OK || hr == E_ABORT, "unexpected result %s", HrToString(hr).c_str());
}

E2E_TEST(http_cancel_during_retry_backoff)
{
    // Cancelling while the call is waiting to retry (Retry-After: 10) completes it promptly.
    const std::string id = UniqueId("cancelretry");
    Call call;
    call.Url("GET", HttpUrl("/retry-after/5?status=503&after=10&id=" + id));
    E2E_CHECK_HR(HCHttpCallRequestSetRetryDelay(call, 1));
    E2E_CHECK_HR(HCHttpCallRequestSetTimeoutWindow(call, 60));
    auto* async = new XAsyncBlock{};
    E2E_CHECK_HR(HCHttpCallPerformAsync(call, async));
    // Wait until the server has answered the first attempt, then give the client a moment to
    // schedule the backoff.
    bool firstAttemptSeen = false;
    for (int i = 0; i < 200 && !firstAttemptSeen; ++i)
    {
        firstAttemptSeen = JsonUInt(FetchString(HttpUrl("/retry-count?id=" + id)), "count") >= 1;
        if (!firstAttemptSeen)
        {
            platform::SleepMs(25);
        }
    }
    E2E_CHECK_MSG(firstAttemptSeen, "the first attempt never reached the server");
    platform::SleepMs(300);
    auto start = std::chrono::steady_clock::now();
    XAsyncCancel(async);
    HRESULT hr = WaitAsync(async, std::chrono::seconds{ 15 });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (hr == E_PENDING)
    {
        PoisonProcess("HTTP call cancelled during retry backoff never completed");
        Fail(__FILE__, __LINE__, "the call cancelled during retry backoff did not complete within 15 s");
    }
    delete async;
    Info("call cancelled during backoff completed with hr=%s after %.2f s", HrToString(hr).c_str(), seconds);
    E2E_CHECK_EQ(E_ABORT, hr);
    E2E_CHECK_MSG(seconds < 3.0, "cancellation during backoff took %.2f s", seconds);
}

E2E_TEST(http_close_handle_while_inflight)
{
    auto* async = new XAsyncBlock{};
    {
        HCCallHandle call = nullptr;
        E2E_CHECK_HR(HCHttpCallCreate(&call));
        E2E_CHECK_HR(HCHttpCallRequestSetUrl(call, "GET", HttpUrl("/delay/500").c_str()));
        E2E_CHECK_HR(HCHttpCallRequestSetRetryAllowed(call, false));
        E2E_CHECK_HR(HCHttpCallPerformAsync(call, async));
        // The library holds its own reference while the call is in flight.
        E2E_CHECK_HR(HCHttpCallCloseHandle(call));
    }
    HRESULT hr = WaitAsync(async, std::chrono::seconds{ 10 });
    if (hr == E_PENDING)
    {
        PoisonProcess("HTTP call never completed after its handle was closed");
        Fail(__FILE__, __LINE__, "call did not complete after its handle was closed");
    }
    delete async;
    E2E_CHECK_HR(hr);
}

E2E_TEST_SELF_INIT(http_cleanup_with_inflight_call)
{
    // HCCleanupAsync cancels in-flight calls, but cancelling a call whose WinHTTP request is
    // already running doesn't abort the request (see http_cancel_inflight), so cleanup completes
    // once that request finishes or hits its timeout. Use a 3 s response and check cleanup waits
    // for it and then completes, and the call completes too.
    E2E_CHECK_HR(HCInitialize(nullptr));
    HCCallHandle call = nullptr;
    E2E_CHECK_HR(HCHttpCallCreate(&call));
    E2E_CHECK_HR(HCHttpCallRequestSetUrl(call, "GET", HttpUrl("/delay/3000").c_str()));
    E2E_CHECK_HR(HCHttpCallRequestSetRetryAllowed(call, false));
    auto* async = new XAsyncBlock{};
    E2E_CHECK_HR(HCHttpCallPerformAsync(call, async));
    E2E_CHECK_HR(HCHttpCallCloseHandle(call));
    platform::SleepMs(300);

    auto start = std::chrono::steady_clock::now();
    HRESULT cleanupHr = CleanupWithTimeout(std::chrono::seconds{ 15 });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (cleanupHr == E_PENDING)
    {
        PoisonProcess("HCCleanupAsync hung with an in-flight HTTP call");
        Fail(__FILE__, __LINE__, "HCCleanupAsync did not complete within 15 s with an in-flight 3 s call");
    }
    HRESULT callHr = WaitAsync(async, std::chrono::seconds{ 5 });
    if (callHr == E_PENDING)
    {
        PoisonProcess("in-flight call never completed after HCCleanupAsync");
        Fail(__FILE__, __LINE__, "the in-flight call never completed after HCCleanupAsync");
    }
    delete async;
    Info("HCCleanupAsync hr=%s after %.2f s; in-flight call completed with hr=%s", HrToString(cleanupHr).c_str(), seconds, HrToString(callHr).c_str());
    E2E_CHECK_HR(cleanupHr);
    E2E_CHECK(callHr == S_OK || callHr == E_ABORT);
    E2E_CHECK(!HCIsInitialized());
}

E2E_TEST_SELF_INIT(http_global_request_limit_queues)
{
    // Set before HCInitialize; restored to the device default (0) before returning.
    const uint32_t limit = 2;
    const int calls = 6;
    HRESULT setHr = HCSettingsSetGlobalRequestLimit(limit);
    if (setHr == E_NOTIMPL)
    {
        // Documented: only the WinHTTP provider (Win32, GDK) implements admission control.
        Skip(std::string{ "HCSettingsSetGlobalRequestLimit returns E_NOTIMPL with the " } + HttpProviderName + " provider (WinHTTP only, by design)");
    }
    E2E_CHECK_HR(setHr);
    struct RestoreLimit { ~RestoreLimit() { HCSettingsSetGlobalRequestLimit(0); } } restore;
    E2E_CHECK_HR(HCInitialize(nullptr));
    struct Cleanup { ~Cleanup() { if (HCIsInitialized()) { CleanupWithTimeout(std::chrono::seconds{ 20 }); } } } cleanup;

    uint32_t readBack = 0;
    E2E_CHECK_HR(HCSettingsGetGlobalRequestLimit(&readBack));
    E2E_CHECK_EQ(limit, readBack);

    const std::string group = UniqueId("limit");
    std::vector<std::unique_ptr<Call>> handles;
    std::vector<XAsyncBlock*> asyncs;
    for (int i = 0; i < calls; ++i)
    {
        handles.push_back(std::make_unique<Call>());
        handles.back()->Url("GET", HttpUrl("/concurrency/400?group=" + group)).NoRetry();
        asyncs.push_back(new XAsyncBlock{});
        E2E_CHECK_HR(HCHttpCallPerformAsync(*handles.back(), asyncs.back()));
    }
    for (int i = 0; i < calls; ++i)
    {
        HRESULT hr = WaitAsync(asyncs[i], std::chrono::seconds{ 30 });
        if (hr == E_PENDING)
        {
            PoisonProcess("a queued HTTP call never completed");
            Fail(__FILE__, __LINE__, "queued call did not complete");
        }
        delete asyncs[i];
        E2E_CHECK_MSG(SUCCEEDED(hr) && handles[i]->Status() == 200, "call %d: hr=%s %s", i, HrToString(hr).c_str(), handles[i]->Describe().c_str());
    }

    std::string stats = FetchString(HttpUrl("/concurrency-stats?group=" + group));
    uint64_t maxActive = JsonUInt(stats, "max");
    Info("server saw at most %llu concurrent requests (limit %u, %d calls)", static_cast<unsigned long long>(maxActive), limit, calls);
    E2E_CHECK_EQ(static_cast<uint64_t>(calls), JsonUInt(stats, "total"));
    E2E_CHECK_MSG(maxActive <= limit, "server saw %llu concurrent requests with a limit of %u", static_cast<unsigned long long>(maxActive), limit);
}
