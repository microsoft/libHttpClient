// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "HTTP/compression.h"
#include "Platform/ExternalHttpProvider.h"
#include <httpClient/httpProvider.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#if !HC_NOZLIB
#include <zlib.h>
#endif

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

#if !HC_NOZLIB

namespace
{

using Bytes = std::vector<uint8_t>;

constexpr uint8_t kGzipMagic0 = 0x1f;
constexpr uint8_t kGzipMagic1 = 0x8b;
constexpr size_t kGzipXflOffset = 8; // RFC 1952 header: ID1 ID2 CM FLG MTIME(4) XFL OS

Bytes ToBytes(std::string const& s)
{
    return Bytes(s.begin(), s.end());
}

std::string ToString(Bytes const& b)
{
    return std::string(b.begin(), b.end());
}

// Highly compressible text.
std::string MakeText(size_t size)
{
    static char const pattern[] = "The quick brown fox jumps over the lazy dog. 0123456789\n";
    std::string s;
    s.reserve(size);
    while (s.size() < size)
    {
        s.push_back(pattern[s.size() % (sizeof(pattern) - 1)]);
    }
    return s;
}

// Incompressible bytes, so the compressed output spans many of the library's 16 KB chunks.
Bytes MakeRandomBytes(size_t size, uint32_t seed)
{
    std::mt19937 rng{ seed };
    std::uniform_int_distribution<int> dist{ 0, 255 };
    Bytes b(size);
    for (auto& v : b)
    {
        v = static_cast<uint8_t>(dist(rng));
    }
    return b;
}

// Independent gzip encoder used to build compressed responses.
Bytes Gzip(Bytes const& in, int level = Z_DEFAULT_COMPRESSION)
{
    z_stream stream{};
    if (deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return {};
    }
    Bytes out(deflateBound(&stream, static_cast<uLong>(in.size())) + 64);
    stream.next_in = const_cast<Bytef*>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    int ret = deflate(&stream, Z_FINISH);
    out.resize(out.size() - stream.avail_out);
    deflateEnd(&stream);
    return ret == Z_STREAM_END ? out : Bytes{};
}

// Independent gzip decoder used to check compressed requests. Returns false unless the input is exactly one
// complete, valid gzip member.
bool Gunzip(Bytes const& in, Bytes& out)
{
    out.clear();
    z_stream stream{};
    if (inflateInit2(&stream, 15 | 16) != Z_OK)
    {
        return false;
    }
    stream.next_in = const_cast<Bytef*>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());
    int ret = Z_OK;
    uint8_t buffer[4096];
    while (ret == Z_OK)
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        ret = inflate(&stream, Z_NO_FLUSH);
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
    }
    bool ok = (ret == Z_STREAM_END) && (stream.avail_in == 0);
    inflateEnd(&stream);
    return ok;
}

bool HasGzipMagic(Bytes const& b)
{
    return b.size() > kGzipXflOffset && b[0] == kGzipMagic0 && b[1] == kGzipMagic1;
}

// What the fake provider saw and what it should answer with.
struct FakeProvider
{
    // Response to produce
    uint32_t statusCode{ 200 };
    Bytes responseBody;
    std::string responseContentEncoding; // empty -> header not sent
    std::string responseContentEncodingHeader{ "Content-Encoding" };
    size_t responseWriteChunkSize{ 0 };  // 0 -> one write

    // Captured request
    uint32_t performCount{ 0 };
    bool sawContentEncoding{ false };
    std::string contentEncoding;
    Bytes bodyFromBytes;        // HCHttpCallRequestGetRequestBodyBytes
    Bytes bodyFromReadFunction; // HCHttpCallRequestGetRequestBodyReadFunction, as real providers do
    size_t reportedBodySize{ 0 };
    HRESULT providerHr{ S_OK };
};

void CALLBACK FakePerform(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* ctx,
    _In_opt_ HCPerformEnv /*env*/
)
{
    auto provider = static_cast<FakeProvider*>(ctx);
    ++provider->performCount;

    HRESULT hr = S_OK;
    const char* encoding{ nullptr };
    if (SUCCEEDED(hr)) hr = HCHttpCallRequestGetHeader(call, "Content-Encoding", &encoding);
    provider->sawContentEncoding = encoding != nullptr;
    provider->contentEncoding = encoding ? encoding : "";

    const uint8_t* bytes{ nullptr };
    uint32_t bytesSize{ 0 };
    if (SUCCEEDED(hr)) hr = HCHttpCallRequestGetRequestBodyBytes(call, &bytes, &bytesSize);
    provider->bodyFromBytes = bytes ? Bytes(bytes, bytes + bytesSize) : Bytes{};

    HCHttpCallRequestBodyReadFunction readFunction{ nullptr };
    size_t bodySize{ 0 };
    void* readContext{ nullptr };
    if (SUCCEEDED(hr)) hr = HCHttpCallRequestGetRequestBodyReadFunction(call, &readFunction, &bodySize, &readContext);
    provider->reportedBodySize = bodySize;
    provider->bodyFromReadFunction.clear();
    if (SUCCEEDED(hr) && readFunction && bodySize > 0)
    {
        provider->bodyFromReadFunction.resize(bodySize);
        size_t written{ 0 };
        hr = readFunction(call, 0, bodySize, readContext, provider->bodyFromReadFunction.data(), &written);
        provider->bodyFromReadFunction.resize(written);
    }

    if (SUCCEEDED(hr)) hr = HCHttpCallResponseSetStatusCode(call, provider->statusCode);
    if (SUCCEEDED(hr) && !provider->responseContentEncoding.empty())
    {
        hr = HCHttpCallResponseSetHeader(call, provider->responseContentEncodingHeader.c_str(), provider->responseContentEncoding.c_str());
    }

    if (SUCCEEDED(hr) && !provider->responseBody.empty())
    {
        // Deliver the body through the write function, like the real providers do.
        HCHttpCallResponseBodyWriteFunction writeFunction{ nullptr };
        void* writeContext{ nullptr };
        hr = HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &writeContext);
        size_t const chunk = provider->responseWriteChunkSize ? provider->responseWriteChunkSize : provider->responseBody.size();
        for (size_t offset = 0; SUCCEEDED(hr) && offset < provider->responseBody.size(); offset += chunk)
        {
            size_t n = (std::min)(chunk, provider->responseBody.size() - offset);
            hr = writeFunction(call, provider->responseBody.data() + offset, n, writeContext);
        }
    }

    provider->providerHr = hr;
    XAsyncComplete(asyncBlock, S_OK, 0);
}

// Installs the fake provider, initializes the library and owns a thread pool queue. The destructor always
// restores global state, so a failed VERIFY can't leak into later tests.
class CompressionTestEnvironment
{
public:
    CompressionTestEnvironment()
    {
        ExternalHttpProvider::Get().ClearCallback();
        m_hr = HCSetHttpCallPerformFunction(&FakePerform, &provider);
        if (SUCCEEDED(m_hr)) m_hr = HCInitialize(nullptr);
        if (SUCCEEDED(m_hr)) m_initialized = true;
        if (SUCCEEDED(m_hr)) m_hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &m_queue);
    }

    ~CompressionTestEnvironment()
    {
        if (m_call)
        {
            HCHttpCallCloseHandle(m_call);
        }
        if (m_queue)
        {
            XTaskQueueCloseHandle(m_queue);
        }
        if (m_initialized)
        {
            HCCleanup();
        }
        ExternalHttpProvider::Get().ClearCallback();
    }

    CompressionTestEnvironment(CompressionTestEnvironment const&) = delete;
    CompressionTestEnvironment& operator=(CompressionTestEnvironment const&) = delete;

    HRESULT InitResult() const { return m_hr; }

    // Creates a fresh POST call to a dummy URL, closing the previous one.
    HCCallHandle NewCall()
    {
        if (m_call)
        {
            HCHttpCallCloseHandle(m_call);
            m_call = nullptr;
        }
        if (FAILED(HCHttpCallCreate(&m_call)))
        {
            return nullptr;
        }
        HCHttpCallRequestSetUrl(m_call, "POST", "https://example.invalid/compression");
        HCHttpCallRequestSetRetryAllowed(m_call, false);
        return m_call;
    }

    // Performs the call and waits for it to finish. Returns the async result.
    HRESULT Perform(HCCallHandle call)
    {
        XAsyncBlock async{};
        async.queue = m_queue;
        HRESULT hr = HCHttpCallPerformAsync(call, &async);
        if (FAILED(hr))
        {
            return hr;
        }
        return XAsyncGetStatus(&async, true);
    }

    FakeProvider provider;

private:
    HRESULT m_hr{ E_FAIL };
    bool m_initialized{ false };
    XTaskQueueHandle m_queue{ nullptr };
    HCCallHandle m_call{ nullptr };
};

Bytes GetResponseBytes(HCCallHandle call)
{
    size_t size{ 0 };
    if (FAILED(HCHttpCallResponseGetResponseBodyBytesSize(call, &size)))
    {
        return {};
    }
    Bytes b(size);
    size_t used{ 0 };
    if (size > 0 && FAILED(HCHttpCallResponseGetResponseBodyBytes(call, size, b.data(), &used)))
    {
        return {};
    }
    b.resize(used);
    return b;
}

std::string GetResponseString(HCCallHandle call)
{
    const char* s{ nullptr };
    if (FAILED(HCHttpCallResponseGetResponseString(call, &s)) || s == nullptr)
    {
        return "<error>";
    }
    return s;
}

// Request body source for HCHttpCallRequestSetRequestBodyReadFunction tests.
struct BodySource
{
    Bytes data;
    HRESULT failWith{ S_OK };
    size_t shortBy{ 0 };
    uint32_t reads{ 0 };
};

HRESULT CALLBACK ReadFromBodySource(
    _In_ HCCallHandle /*call*/,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
)
{
    auto source = static_cast<BodySource*>(context);
    ++source->reads;
    *bytesWritten = 0;
    if (FAILED(source->failWith))
    {
        return source->failWith;
    }
    if (offset > source->data.size())
    {
        return E_FAIL;
    }
    size_t n = (std::min)(bytesAvailable, source->data.size() - offset);
    n = n > source->shortBy ? n - source->shortBy : 0;
    if (n > 0)
    {
        memcpy(destination, source->data.data() + offset, n);
    }
    *bytesWritten = n;
    return S_OK;
}

struct WriteSink
{
    Bytes data;
    uint32_t writes{ 0 };
};

HRESULT CALLBACK WriteToSink(
    _In_ HCCallHandle /*call*/,
    _In_reads_bytes_(bytesAvailable) const uint8_t* source,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context
)
{
    auto sink = static_cast<WriteSink*>(context);
    ++sink->writes;
    sink->data.insert(sink->data.end(), source, source + bytesAvailable);
    return S_OK;
}

void STDAPIVCALLTYPE ReadResponseStringInRoutedHandler(_In_ HCCallHandle call, _In_opt_ void* context)
{
    // Caches HC_CALL::responseString from the (still compressed) body before decompression runs.
    const char* s{ nullptr };
    HCHttpCallResponseGetResponseString(call, &s);
    ++*static_cast<uint32_t*>(context);
}

} // namespace

#endif // !HC_NOZLIB

DEFINE_TEST_CLASS(CompressionTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(CompressionTests);

    DEFINE_TEST_CASE(TestCompressionAvailable)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCompressionAvailable);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call{ nullptr };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        HRESULT noneHr = HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::None);
        HRESULT lowHr = HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Low);
        HRESULT setCompressedHr = HCHttpCallResponseSetGzipCompressed(call, true);
        HCHttpCallCloseHandle(call);
        HCCleanup();

        VERIFY_ARE_EQUAL(S_OK, noneHr);
        VERIFY_ARE_EQUAL(S_OK, setCompressedHr);
#if !HC_NOZLIB
        VERIFY_IS_TRUE(Compression::Available());
        VERIFY_ARE_EQUAL(S_OK, lowHr);
#else
        VERIFY_IS_FALSE(Compression::Available());
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, lowHr);
#endif
    }

#if !HC_NOZLIB

    DEFINE_TEST_CASE(TestGzipRequestBodyRoundTrip)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestBodyRoundTrip);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const body = MakeText(8 * 1024);
        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, body.c_str()));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Medium));

        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));

        auto& p = env.provider;
        VERIFY_ARE_EQUAL(S_OK, p.providerHr);
        VERIFY_ARE_EQUAL_UINT(1u, p.performCount);
        VERIFY_IS_TRUE(p.sawContentEncoding);
        VERIFY_ARE_EQUAL_STR("gzip", p.contentEncoding.c_str());
        VERIFY_IS_TRUE(HasGzipMagic(p.bodyFromBytes));
        VERIFY_IS_LESS_THAN(p.bodyFromBytes.size(), body.size());
        // The body size and the default read function both reflect the compressed bytes.
        VERIFY_ARE_EQUAL_UINT(p.bodyFromBytes.size(), p.reportedBodySize);
        VERIFY_IS_TRUE(p.bodyFromBytes == p.bodyFromReadFunction);

        Bytes decompressed;
        VERIFY_IS_TRUE(Gunzip(p.bodyFromBytes, decompressed));
        VERIFY_IS_TRUE(ToString(decompressed) == body);

        // The response is untouched by request compression.
        uint32_t status{ 0 };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &status));
        VERIFY_ARE_EQUAL_UINT(200u, status);
    }

    DEFINE_TEST_CASE(TestGzipRequestCompressionLevels)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestCompressionLevels);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const body = MakeText(32 * 1024);

        // zlib records the level class in the gzip header's XFL byte: 4 = fastest (level 1), 2 = maximum
        // (level 9), 0 = anything else. That proves HCCompressionLevel reaches deflateInit2 unchanged.
        struct LevelCase { HCCompressionLevel level; uint8_t expectedXfl; };
        LevelCase const cases[] =
        {
            { HCCompressionLevel::Low, 4 },
            { HCCompressionLevel::Medium, 0 },
            { HCCompressionLevel::High, 2 },
        };

        size_t lowSize{ 0 };
        size_t highSize{ 0 };
        for (auto const& c : cases)
        {
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, body.c_str()));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, c.level));
            VERIFY_ARE_EQUAL(S_OK, env.Perform(call));

            auto& p = env.provider;
            VERIFY_ARE_EQUAL_STR("gzip", p.contentEncoding.c_str());
            VERIFY_IS_TRUE(HasGzipMagic(p.bodyFromBytes));
            VERIFY_ARE_EQUAL_UINT(c.expectedXfl, p.bodyFromBytes[kGzipXflOffset]);
            Bytes decompressed;
            VERIFY_IS_TRUE(Gunzip(p.bodyFromBytes, decompressed));
            VERIFY_IS_TRUE(ToString(decompressed) == body);

            if (c.level == HCCompressionLevel::Low) lowSize = p.bodyFromBytes.size();
            if (c.level == HCCompressionLevel::High) highSize = p.bodyFromBytes.size();
        }
        VERIFY_IS_GREATER_THAN_OR_EQUAL(lowSize, highSize);

        // HCCompressionLevel::None (also the default) sends the body as-is, with no Content-Encoding.
        for (bool explicitNone : { true, false })
        {
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, body.c_str()));
            if (explicitNone)
            {
                VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::None));
            }
            VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
            VERIFY_IS_FALSE(env.provider.sawContentEncoding);
            VERIFY_IS_TRUE(env.provider.bodyFromBytes == ToBytes(body));
        }
    }

    DEFINE_TEST_CASE(TestGzipRequestSmallBodyIsStillCompressed)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestSmallBodyIsStillCompressed);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        // There is no minimum-size threshold: even a 1-byte body is gzipped (and grows, due to the
        // 18-byte gzip header/trailer).
        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        uint8_t const one[] = { 'a' };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyBytes(call, one, 1));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::High));
        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));

        auto& p = env.provider;
        VERIFY_ARE_EQUAL_STR("gzip", p.contentEncoding.c_str());
        VERIFY_IS_TRUE(HasGzipMagic(p.bodyFromBytes));
        VERIFY_IS_GREATER_THAN_OR_EQUAL(p.bodyFromBytes.size(), static_cast<size_t>(18 + 1));
        Bytes decompressed;
        VERIFY_IS_TRUE(Gunzip(p.bodyFromBytes, decompressed));
        VERIFY_ARE_EQUAL_UINT(1u, decompressed.size());
        VERIFY_ARE_EQUAL_UINT('a', decompressed[0]);
    }

    DEFINE_TEST_CASE(TestGzipRequestWithoutBodyFails)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestWithoutBodyFails);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        // Current behaviour: enabling compression on a call with no body fails the perform with
        // E_INVALIDARG ("Request body is empty") before the provider is invoked.
        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Low));
        VERIFY_ARE_EQUAL(E_INVALIDARG, env.Perform(call));
        VERIFY_ARE_EQUAL_UINT(0u, env.provider.performCount);
    }

    DEFINE_TEST_CASE(TestGzipRequestCustomReadFunction)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestCustomReadFunction);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        BodySource source;
        source.data = ToBytes(MakeText(20 * 1024));

        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyReadFunction(call, &ReadFromBodySource, source.data.size(), &source));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Medium));
        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));

        // The client's read function is drained once, then the provider reads the compressed bytes through
        // the default read function.
        VERIFY_ARE_EQUAL_UINT(1u, source.reads);
        auto& p = env.provider;
        VERIFY_ARE_EQUAL_STR("gzip", p.contentEncoding.c_str());
        VERIFY_ARE_EQUAL_UINT(p.bodyFromBytes.size(), p.reportedBodySize);
        VERIFY_IS_TRUE(p.bodyFromReadFunction == p.bodyFromBytes);
        Bytes decompressed;
        VERIFY_IS_TRUE(Gunzip(p.bodyFromReadFunction, decompressed));
        VERIFY_IS_TRUE(decompressed == source.data);
    }

    DEFINE_TEST_CASE(TestGzipRequestReadFunctionErrors)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipRequestReadFunctionErrors);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        // A failing read function fails the perform with its HRESULT; the provider is never invoked.
        {
            BodySource source;
            source.data = ToBytes(MakeText(1024));
            source.failWith = E_ACCESSDENIED;
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyReadFunction(call, &ReadFromBodySource, source.data.size(), &source));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Low));
            VERIFY_ARE_EQUAL(E_ACCESSDENIED, env.Perform(call));
            VERIFY_ARE_EQUAL_UINT(1u, source.reads);
        }

        // A read function that supplies fewer bytes than the declared body size fails with E_FAIL.
        {
            BodySource source;
            source.data = ToBytes(MakeText(1024));
            source.shortBy = 10;
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyReadFunction(call, &ReadFromBodySource, source.data.size(), &source));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Low));
            VERIFY_ARE_EQUAL(E_FAIL, env.Perform(call));
        }

        VERIFY_ARE_EQUAL_UINT(0u, env.provider.performCount);
    }

    DEFINE_TEST_CASE(TestGzipResponseDecompressed)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipResponseDecompressed);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const original = MakeText(40 * 1024);
        env.provider.responseBody = Gzip(ToBytes(original));
        env.provider.responseContentEncoding = "gzip";
        env.provider.responseWriteChunkSize = 1000; // provider delivers the compressed body in pieces
        VERIFY_IS_TRUE(HasGzipMagic(env.provider.responseBody));

        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));
        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
        VERIFY_ARE_EQUAL(S_OK, env.provider.providerHr);

        VERIFY_IS_TRUE(GetResponseString(call) == original);
        VERIFY_IS_TRUE(GetResponseBytes(call) == ToBytes(original));

        // Header names are case-insensitive, so a lower-case "content-encoding" (as HTTP/2 sends it) works too.
        env.provider.responseContentEncodingHeader = "content-encoding";
        call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));
        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
        VERIFY_IS_TRUE(GetResponseString(call) == original);
    }

    DEFINE_TEST_CASE(TestGzipResponseNotDecompressedWhenNotApplicable)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipResponseNotDecompressedWhenNotApplicable);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const original = MakeText(4096);
        Bytes const compressed = Gzip(ToBytes(original));

        struct Case
        {
            bool setGzipCompressed;
            char const* contentEncoding; // nullptr -> not sent
            Bytes body;
            Bytes expected;
        };
        Case const cases[] =
        {
            // Content-Encoding absent -> body left as received.
            { true, nullptr, compressed, compressed },
            // A different coding -> body left as received.
            { true, "deflate", compressed, compressed },
            // HCHttpCallResponseSetGzipCompressed not called (the default) -> body left as received.
            { false, "gzip", compressed, compressed },
            // Already-decoded body (no gzip magic) with Content-Encoding: gzip -> passed through untouched.
            { true, "gzip", ToBytes(original), ToBytes(original) },
        };

        for (auto const& c : cases)
        {
            env.provider.responseBody = c.body;
            env.provider.responseContentEncoding = c.contentEncoding ? c.contentEncoding : "";
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            if (c.setGzipCompressed)
            {
                VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));
            }
            VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
            VERIFY_IS_TRUE(GetResponseBytes(call) == c.expected);
        }
    }

    DEFINE_TEST_CASE(TestGzipResponseCorrupt)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipResponseCorrupt);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        Bytes const compressed = Gzip(ToBytes(MakeText(64 * 1024)));
        VERIFY_IS_TRUE(compressed.size() > 64);

        // Valid gzip magic followed by garbage.
        Bytes garbage = MakeRandomBytes(2048, 1);
        garbage[0] = kGzipMagic0;
        garbage[1] = kGzipMagic1;
        garbage[2] = 8; // CM = deflate
        garbage[3] = 0; // FLG

        // Valid stream with a broken CRC32 trailer.
        Bytes badCrc = compressed;
        badCrc[badCrc.size() - 8] ^= 0xFF;

        // Valid stream cut in half. This used to make DecompressFromGzip loop forever (and grow its output buffer
        // without bound), because Z_BUF_ERROR was treated as "keep going".
        Bytes truncated(compressed.begin(), compressed.begin() + compressed.size() / 2);

        // Current behaviour: a gzip body that fails to decode does not fail the call. The perform succeeds with the
        // provider's status code and no network error, and the response body is empty.
        for (Bytes const* body : { &garbage, &badCrc, &truncated })
        {
            env.provider.responseBody = *body;
            env.provider.responseContentEncoding = "gzip";
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));
            VERIFY_ARE_EQUAL(S_OK, env.Perform(call));

            HRESULT networkError{ E_FAIL };
            uint32_t platformError{ 1 };
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &networkError, &platformError));
            VERIFY_ARE_EQUAL(S_OK, networkError);
            uint32_t status{ 0 };
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &status));
            VERIFY_ARE_EQUAL_UINT(200u, status);
            VERIFY_ARE_EQUAL_UINT(0u, GetResponseBytes(call).size());
            VERIFY_IS_TRUE(GetResponseString(call).empty());
        }
    }

    DEFINE_TEST_CASE(TestGzipResponseCustomWriteFunction)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipResponseCustomWriteFunction);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const original = MakeText(50 * 1024);
        env.provider.responseBody = Gzip(ToBytes(original));
        env.provider.responseContentEncoding = "gzip";
        env.provider.responseWriteChunkSize = 4096;

        WriteSink sink;
        HCCallHandle call = env.NewCall();
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetResponseBodyWriteFunction(call, &WriteToSink, &sink));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));
        VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
        VERIFY_ARE_EQUAL(S_OK, env.provider.providerHr);

        // The library buffers the compressed body itself, then hands the client's write function the decompressed
        // body in a single write, and restores the client's write function afterwards.
        VERIFY_ARE_EQUAL_UINT(1u, sink.writes);
        VERIFY_IS_TRUE(sink.data == ToBytes(original));

        HCHttpCallResponseBodyWriteFunction writeFunction{ nullptr };
        void* writeContext{ nullptr };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &writeContext));
        VERIFY_IS_TRUE(writeFunction == &WriteToSink);
        VERIFY_IS_TRUE(writeContext == &sink);
    }

    DEFINE_TEST_CASE(TestGzipResponseStringNotStaleAfterRoutedHandler)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipResponseStringNotStaleAfterRoutedHandler);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        std::string const original = MakeText(4096);
        env.provider.responseBody = Gzip(ToBytes(original));
        env.provider.responseContentEncoding = "gzip";

        // Routed handlers run before decompression; one that reads the response string must not leave the
        // compressed bytes cached as the final response string.
        uint32_t handlerCalls{ 0 };
        int32_t handlerId = HCAddCallRoutedHandler(&ReadResponseStringInRoutedHandler, &handlerCalls);

        HCCallHandle call = env.NewCall();
        HRESULT setHr = call ? HCHttpCallResponseSetGzipCompressed(call, true) : E_FAIL;
        HRESULT performHr = SUCCEEDED(setHr) ? env.Perform(call) : setHr;
        HCRemoveCallRoutedHandler(handlerId);

        VERIFY_ARE_EQUAL(S_OK, performHr);
        VERIFY_ARE_EQUAL_UINT(1u, handlerCalls);
        VERIFY_IS_TRUE(GetResponseBytes(call) == ToBytes(original));
        VERIFY_IS_TRUE(GetResponseString(call) == original);
    }

    DEFINE_TEST_CASE(TestGzipLargeRoundTrip)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGzipLargeRoundTrip);

        CompressionTestEnvironment env;
        VERIFY_ARE_EQUAL(S_OK, env.InitResult());

        size_t const oneMb = 1024 * 1024;
        // Incompressible data makes the compressed output ~1 MB, i.e. ~64 of the library's 16 KB chunks in each
        // direction; text exercises a high compression ratio.
        Bytes const payloads[] = { MakeRandomBytes(oneMb, 42), ToBytes(MakeText(oneMb)) };

        for (Bytes const& payload : payloads)
        {
            // Request: compress 1 MB.
            HCCallHandle call = env.NewCall();
            VERIFY_IS_NOT_NULL(call);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyBytes(call, payload.data(), static_cast<uint32_t>(payload.size())));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::High));

            // Response: echo the payload back gzipped, in 64 KB writes.
            env.provider.responseBody = Gzip(payload);
            env.provider.responseContentEncoding = "gzip";
            env.provider.responseWriteChunkSize = 64 * 1024;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetGzipCompressed(call, true));

            VERIFY_ARE_EQUAL(S_OK, env.Perform(call));
            VERIFY_ARE_EQUAL(S_OK, env.provider.providerHr);

            Bytes decompressedRequest;
            VERIFY_IS_TRUE(Gunzip(env.provider.bodyFromBytes, decompressedRequest));
            VERIFY_IS_TRUE(decompressedRequest == payload);
            VERIFY_IS_TRUE(env.provider.bodyFromReadFunction == env.provider.bodyFromBytes);

            VERIFY_IS_TRUE(GetResponseBytes(call) == payload);
        }
    }

#endif // !HC_NOZLIB
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
