// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Request/response body streaming and progress reporting (test-gap plan item 1.4).
//
// The unit tests run against the Generic platform, which has no network stack, so these tests install
// a fake HTTP provider through HCSetHttpCallPerformFunction. The fake behaves like the in-box providers
// (WinHTTP/Curl): it pulls the request body through HCHttpCallRequestGetRequestBodyReadFunction in
// chunks, pushes the response body through HCHttpCallResponseGetResponseBodyWriteFunction in chunks, and
// reports progress through HCHttpCallRequestGetProgressReportFunction (using the dynamic-size getters
// when a dynamic size is set). That exercises both the client-facing setters and the provider-facing
// getters of each API pair.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "HTTP/httpcall.h"
#include "Platform/ExternalHttpProvider.h"
#include <httpClient/httpProvider.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

using Bytes = std::vector<uint8_t>;

// Distinctive failure codes so a test can tell which component failed the call.
const HRESULT c_readerFailure = static_cast<HRESULT>(0x8007001EL);   // HRESULT_FROM_WIN32(ERROR_READ_FAULT)
const HRESULT c_writerFailure = static_cast<HRESULT>(0x80070070L);   // HRESULT_FROM_WIN32(ERROR_DISK_FULL)
const HRESULT c_progressFailure = static_cast<HRESULT>(0x800704C7L); // HRESULT_FROM_WIN32(ERROR_CANCELLED)

Bytes ToBytes(const char* text)
{
    return Bytes(reinterpret_cast<const uint8_t*>(text), reinterpret_cast<const uint8_t*>(text) + strlen(text));
}

uint8_t PatternByte(size_t index)
{
    return static_cast<uint8_t>((index * 37 + 11) & 0xFF);
}

Bytes MakePattern(size_t size)
{
    Bytes bytes(size);
    for (size_t i = 0; i < size; ++i)
    {
        bytes[i] = PatternByte(i);
    }
    if (size > 1)
    {
        bytes[1] = 0; // make sure embedded NULs survive the binary paths
    }
    return bytes;
}

// Initializes libHttpClient with an optional external perform callback and always undoes it.
class ProviderScope
{
public:
    ProviderScope(HCCallPerformFunction perform, void* context) noexcept
    {
        ExternalHttpProvider::Get().ClearCallback();
        if (perform != nullptr)
        {
            m_initHr = HCSetHttpCallPerformFunction(perform, context);
        }
        if (SUCCEEDED(m_initHr))
        {
            m_initHr = HCInitialize(nullptr);
        }
    }

    ~ProviderScope()
    {
        HCCleanup();
        ExternalHttpProvider::Get().ClearCallback();
    }

    ProviderScope(const ProviderScope&) = delete;
    ProviderScope& operator=(const ProviderScope&) = delete;

    HRESULT InitHr() const noexcept { return m_initHr; }

private:
    HRESULT m_initHr{ S_OK };
};

struct ScopedCall
{
    ScopedCall() = default;
    ScopedCall(const ScopedCall&) = delete;
    ScopedCall& operator=(const ScopedCall&) = delete;
    ~ScopedCall()
    {
        if (handle != nullptr)
        {
            HCHttpCallCloseHandle(handle);
        }
    }

    HCCallHandle handle{ nullptr };
};

HRESULT PerformAndWait(HCCallHandle call)
{
    XTaskQueueHandle queue{ nullptr };
    HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
    if (FAILED(hr))
    {
        return hr;
    }

    XAsyncBlock asyncBlock{};
    asyncBlock.queue = queue;
    hr = HCHttpCallPerformAsync(call, &asyncBlock);
    if (SUCCEEDED(hr))
    {
        hr = XAsyncGetStatus(&asyncBlock, true);
    }

    XTaskQueueCloseHandle(queue);
    return hr;
}

struct ProgressRecord
{
    HCCallHandle call;
    uint64_t current;
    uint64_t total;
};

struct ProgressRecorder
{
    HRESULT returnHr{ S_OK };
    std::vector<ProgressRecord> records;
};

HRESULT CALLBACK RecordProgress(
    _In_ HCCallHandle call,
    _In_ uint64_t current,
    _In_ uint64_t total,
    _In_opt_ void* context
)
{
    auto recorder = static_cast<ProgressRecorder*>(context);
    recorder->records.push_back(ProgressRecord{ call, current, total });
    return recorder->returnHr;
}

// Fake HTTP provider. All observations are written on the provider thread before XAsyncComplete and
// read by the test thread after XAsyncGetStatus(wait = true) returns.
struct FakeServer
{
    // Behaviour
    size_t readChunkSize{ 7 };
    size_t writeChunkSize{ 5 };
    uint32_t statusCode{ 200 };
    Bytes responseBody;
    bool respondWithSetAndAppend{ false };

    // Observations
    uint32_t performCount{ 0 };
    HCHttpCallRequestBodyReadFunction readFunction{ nullptr };
    void* readContext{ nullptr };
    size_t readBodySize{ 0 };
    const uint8_t* bodyBytesPointer{ nullptr };
    uint32_t bodyBytesSize{ 0 };
    Bytes bodyBytesSeen;
    std::vector<size_t> readOffsets;
    Bytes requestBody;
    HCHttpCallResponseBodyWriteFunction writeFunction{ nullptr };
    void* writeContext{ nullptr };
    size_t writeCount{ 0 };
    HRESULT appendProbeHr{ E_PENDING };
    HRESULT responseSetDynamicSizeHr{ E_PENDING };
    size_t uploadInterval{ 0 };
    size_t downloadInterval{ 0 };
};

// Mirrors WinHttpConnection::ReportProgress / CurlEasyRequest::ProgressReportCallback without the
// minimum-interval throttling (which is provider-owned and time based).
HRESULT ReportProgress(HCCallHandle call, bool isUpload, uint64_t current, uint64_t total)
{
    HCHttpCallProgressReportFunction progressFunction{ nullptr };
    size_t interval{ 0 };
    void* progressContext{ nullptr };
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(call, isUpload, &progressFunction, &interval, &progressContext);
    if (FAILED(hr) || progressFunction == nullptr)
    {
        return hr;
    }

    size_t dynamicSize{ 0 };
    size_t dynamicWritten{ 0 };
    hr = isUpload ?
        HCHttpCallRequestGetDynamicBytesWritten(call, &dynamicSize, &dynamicWritten) :
        HCHttpCallResponseGetDynamicBytesWritten(call, &dynamicSize, &dynamicWritten);
    if (FAILED(hr))
    {
        return hr;
    }

    if (dynamicSize > 0)
    {
        total = dynamicSize;
        current = dynamicWritten;
    }

    return progressFunction(call, current, total, progressContext);
}

HRESULT FakeReadRequestBody(HCCallHandle call, FakeServer& server)
{
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(call, &server.readFunction, &server.readBodySize, &server.readContext);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = HCHttpCallRequestGetRequestBodyBytes(call, &server.bodyBytesPointer, &server.bodyBytesSize);
    if (FAILED(hr))
    {
        return hr;
    }
    if (server.bodyBytesPointer != nullptr)
    {
        server.bodyBytesSeen.assign(server.bodyBytesPointer, server.bodyBytesPointer + server.bodyBytesSize);
    }

    HCHttpCallProgressReportFunction unusedFunction{ nullptr };
    void* unusedContext{ nullptr };
    hr = HCHttpCallRequestGetProgressReportFunction(call, true, &unusedFunction, &server.uploadInterval, &unusedContext);
    if (FAILED(hr))
    {
        return hr;
    }

    Bytes chunk(server.readChunkSize);
    size_t offset = 0;
    while (offset < server.readBodySize)
    {
        size_t available = (std::min)(server.readChunkSize, server.readBodySize - offset);
        size_t written = 0;
        server.readOffsets.push_back(offset);
        hr = server.readFunction(call, offset, available, server.readContext, chunk.data(), &written);
        if (FAILED(hr))
        {
            return hr;
        }
        if (written == 0 || written > available)
        {
            return E_UNEXPECTED; // a reader that makes no progress would loop forever
        }

        server.requestBody.insert(server.requestBody.end(), chunk.data(), chunk.data() + written);
        offset += written;

        hr = ReportProgress(call, true, offset, server.readBodySize);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

HRESULT FakeWriteResponse(HCCallHandle call, FakeServer& server)
{
    HRESULT hr = HCHttpCallResponseSetStatusCode(call, server.statusCode);
    if (FAILED(hr))
    {
        return hr;
    }

    // Header name/value are not NUL terminated at the given lengths.
    const char headerName[] = "Content-TypeXYZ";
    const char headerValue[] = "text/plain;garbage";
    hr = HCHttpCallResponseSetHeaderWithLength(call, headerName, 12, headerValue, 10);
    if (FAILED(hr))
    {
        return hr;
    }

    // The response dynamic size is a client-side setting; providers can't change it mid-perform.
    server.responseSetDynamicSizeHr = HCHttpCallResponseSetDynamicSize(call, 1);

    hr = HCHttpCallResponseGetResponseBodyWriteFunction(call, &server.writeFunction, &server.writeContext);
    if (FAILED(hr))
    {
        return hr;
    }

    // Probe whether the provider is allowed to write the body buffer directly.
    static const uint8_t probe = 0;
    server.appendProbeHr = HCHttpCallResponseAppendResponseBodyBytes(call, &probe, 0);

    HCHttpCallProgressReportFunction unusedFunction{ nullptr };
    void* unusedContext{ nullptr };
    hr = HCHttpCallRequestGetProgressReportFunction(call, false, &unusedFunction, &server.downloadInterval, &unusedContext);
    if (FAILED(hr))
    {
        return hr;
    }

    const size_t total = server.responseBody.size();
    if (server.respondWithSetAndAppend)
    {
        const size_t half = total / 2;
        hr = HCHttpCallResponseSetResponseBodyBytes(call, server.responseBody.data(), half);
        if (FAILED(hr))
        {
            return hr;
        }
        hr = HCHttpCallResponseAppendResponseBodyBytes(call, server.responseBody.data() + half, total - half);
        if (FAILED(hr))
        {
            return hr;
        }
        return ReportProgress(call, false, total, total);
    }

    size_t offset = 0;
    while (offset < total)
    {
        size_t count = (std::min)(server.writeChunkSize, total - offset);
        ++server.writeCount;
        hr = server.writeFunction(call, server.responseBody.data() + offset, count, server.writeContext);
        if (FAILED(hr))
        {
            return hr;
        }
        offset += count;

        hr = ReportProgress(call, false, offset, total);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

void CALLBACK FakeServerPerform(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ HCPerformEnv /*env*/
)
{
    auto& server = *static_cast<FakeServer*>(context);
    ++server.performCount;

    HRESULT hr = FakeReadRequestBody(call, server);
    if (SUCCEEDED(hr))
    {
        hr = FakeWriteResponse(call, server);
    }

    XAsyncComplete(asyncBlock, hr, 0);
}

// Client-side custom request body reader that produces PatternByte(offset + i).
struct ReaderState
{
    HCCallHandle expectedCall{ nullptr };
    size_t maxPerRead{ SIZE_MAX };
    size_t failOnRead{ 0 }; // 1-based; 0 = never
    uint64_t dynamicBytesPerRead{ 0 };

    size_t reads{ 0 };
    bool sawUnexpectedCall{ false };
    std::vector<size_t> offsets;
    std::vector<size_t> available;
    HRESULT addDynamicHr{ S_OK };
};

HRESULT CALLBACK PatternReader(
    _In_ HCCallHandle call,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
)
{
    auto& reader = *static_cast<ReaderState*>(context);
    ++reader.reads;
    reader.offsets.push_back(offset);
    reader.available.push_back(bytesAvailable);
    if (call != reader.expectedCall)
    {
        reader.sawUnexpectedCall = true;
    }

    *bytesWritten = 0;
    if (reader.failOnRead != 0 && reader.reads == reader.failOnRead)
    {
        return c_readerFailure;
    }

    size_t count = (std::min)(bytesAvailable, reader.maxPerRead);
    for (size_t i = 0; i < count; ++i)
    {
        destination[i] = PatternByte(offset + i);
    }
    *bytesWritten = count;

    if (reader.dynamicBytesPerRead != 0)
    {
        HRESULT hr = HCHttpCallRequestAddDynamicBytesWritten(call, reader.dynamicBytesPerRead);
        if (FAILED(hr))
        {
            reader.addDynamicHr = hr;
        }
    }

    return S_OK;
}

// Client-side custom response body writer.
struct WriterState
{
    HCCallHandle expectedCall{ nullptr };
    size_t failOnWrite{ 0 }; // 1-based; 0 = never
    uint64_t dynamicBytesPerWrite{ 0 };

    size_t writes{ 0 };
    bool sawUnexpectedCall{ false };
    Bytes received;
    std::vector<size_t> sizes;
    HRESULT addDynamicHr{ S_OK };
};

HRESULT CALLBACK CollectingWriter(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bytesAvailable) const uint8_t* source,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context
)
{
    auto& writer = *static_cast<WriterState*>(context);
    ++writer.writes;
    writer.sizes.push_back(bytesAvailable);
    if (call != writer.expectedCall)
    {
        writer.sawUnexpectedCall = true;
    }

    if (writer.failOnWrite != 0 && writer.writes == writer.failOnWrite)
    {
        return c_writerFailure;
    }

    writer.received.insert(writer.received.end(), source, source + bytesAvailable);

    if (writer.dynamicBytesPerWrite != 0)
    {
        HRESULT hr = HCHttpCallResponseAddDynamicBytesWritten(call, writer.dynamicBytesPerWrite);
        if (FAILED(hr))
        {
            writer.addDynamicHr = hr;
        }
    }

    return S_OK;
}

HRESULT CALLBACK UnusedReader(
    _In_ HCCallHandle call,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
)
{
    (void)call;
    (void)offset;
    (void)bytesAvailable;
    (void)context;
    (void)destination;
    *bytesWritten = 0;
    return E_UNEXPECTED;
}

} // namespace

DEFINE_TEST_CLASS(BodyStreamingTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(BodyStreamingTests);

    DEFINE_TEST_CASE(TestRequestBodyBytesRoundTripThroughProvider)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestBodyBytesRoundTripThroughProvider);

        FakeServer server;
        server.responseBody = ToBytes("hello world");
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/upload"));

        const Bytes body = MakePattern(40);
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyBytes(call.handle, body.data(), static_cast<uint32_t>(body.size())));

        const uint8_t* bodyBytes{ nullptr };
        uint32_t bodySize{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyBytes(call.handle, &bodyBytes, &bodySize));
        VERIFY_ARE_EQUAL(40u, bodySize);
        VERIFY_IS_TRUE(0 == memcmp(bodyBytes, body.data(), body.size()));

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(1u, server.performCount);

        // Provider side: SetRequestBodyBytes is served through the built-in reader.
        VERIFY_IS_TRUE(server.readFunction == &HC_CALL::ReadRequestBody);
        VERIFY_IS_NULL(server.readContext);
        VERIFY_ARE_EQUAL(40u, server.readBodySize);
        VERIFY_IS_TRUE(server.bodyBytesSeen == body);
        VERIFY_IS_TRUE(server.requestBody == body);
        const std::vector<size_t> expectedOffsets{ 0, 7, 14, 21, 28, 35 };
        VERIFY_IS_TRUE(server.readOffsets == expectedOffsets);

        // Provider side: no custom writer, so the built-in writer and direct body APIs are available.
        VERIFY_IS_TRUE(server.writeFunction == &HC_CALL::ResponseBodyWrite);
        VERIFY_IS_NULL(server.writeContext);
        VERIFY_ARE_EQUAL(S_OK, server.appendProbeHr);
        VERIFY_ARE_EQUAL(3u, server.writeCount);

        // Client side response.
        uint32_t statusCode{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetStatusCode(call.handle, &statusCode));
        VERIFY_ARE_EQUAL(200u, statusCode);

        const char* contentType{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetHeader(call.handle, "content-type", &contentType));
        VERIFY_ARE_EQUAL_STR("text/plain", contentType);

        size_t responseSize{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyBytesSize(call.handle, &responseSize));
        VERIFY_ARE_EQUAL(11u, responseSize);

        Bytes responseBytes(responseSize);
        size_t bufferUsed{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyBytes(call.handle, responseBytes.size(), responseBytes.data(), &bufferUsed));
        VERIFY_ARE_EQUAL(11u, bufferUsed);
        VERIFY_IS_TRUE(responseBytes == server.responseBody);

        // bufferUsed is optional; a short buffer is rejected; a null buffer is invalid.
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyBytes(call.handle, responseBytes.size(), responseBytes.data(), nullptr));
        Bytes shortBuffer(responseSize - 1);
        VERIFY_ARE_EQUAL(E_NOT_SUFFICIENT_BUFFER, HCHttpCallResponseGetResponseBodyBytes(call.handle, shortBuffer.size(), shortBuffer.data(), &bufferUsed));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytes(call.handle, responseBytes.size(), nullptr, &bufferUsed));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytesSize(call.handle, nullptr));

        const char* responseString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("hello world", responseString);
    }

    DEFINE_TEST_CASE(TestRequestBodyStringRoundTripThroughProvider)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestBodyStringRoundTripThroughProvider);

        FakeServer server;
        server.responseBody = ToBytes("ok");
        server.readChunkSize = 4;
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/form"));

        const char* text = "name=value&other=1";
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyString(call.handle, text));

        const char* bodyString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyString(call.handle, &bodyString));
        VERIFY_ARE_EQUAL_STR(text, bodyString);

        // Empty and null bodies are rejected rather than clearing the body.
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyString(call.handle, ""));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyString(call.handle, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyBytes(call.handle, nullptr, 4));
        const uint8_t oneByte = 1;
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyBytes(call.handle, &oneByte, 0));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyBytes(nullptr, &oneByte, 1));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyString(call.handle, &bodyString));
        VERIFY_ARE_EQUAL_STR(text, bodyString);

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        VERIFY_ARE_EQUAL(strlen(text), server.readBodySize);
        VERIFY_IS_TRUE(server.requestBody == ToBytes(text));
        VERIFY_IS_TRUE(server.bodyBytesSeen == ToBytes(text));
    }

    DEFINE_TEST_CASE(TestDefaultBodyCallbacks)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestDefaultBodyCallbacks);

        ProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));

        // A new call has an empty body served by the built-in reader, and the built-in writer.
        HCHttpCallRequestBodyReadFunction reader{ nullptr };
        size_t readerBodySize{ 99 };
        void* readerContext{ &readerBodySize };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyReadFunction(call.handle, &reader, &readerBodySize, &readerContext));
        VERIFY_IS_TRUE(reader == &HC_CALL::ReadRequestBody);
        VERIFY_ARE_EQUAL(0u, readerBodySize);
        VERIFY_IS_NULL(readerContext);

        const uint8_t* bodyBytes{ reinterpret_cast<const uint8_t*>(&readerBodySize) };
        uint32_t bodySize{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyBytes(call.handle, &bodyBytes, &bodySize));
        VERIFY_ARE_EQUAL(0u, bodySize);
        VERIFY_IS_NULL(bodyBytes);

        HCHttpCallResponseBodyWriteFunction writer{ nullptr };
        void* writerContext{ &readerBodySize };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyWriteFunction(call.handle, &writer, &writerContext));
        VERIFY_IS_TRUE(writer == &HC_CALL::ResponseBodyWrite);
        VERIFY_IS_NULL(writerContext);

        // Built-in reader semantics: copies from offset, short read at the end, zero bytes at the end,
        // E_FAIL past the end, E_INVALIDARG for bad arguments.
        const Bytes body = MakePattern(10);
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyBytes(call.handle, body.data(), static_cast<uint32_t>(body.size())));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyReadFunction(call.handle, &reader, &readerBodySize, &readerContext));
        VERIFY_ARE_EQUAL(10u, readerBodySize);

        uint8_t destination[8]{};
        size_t written{ 99 };
        VERIFY_SUCCEEDED(reader(call.handle, 0, sizeof(destination), readerContext, destination, &written));
        VERIFY_ARE_EQUAL(8u, written);
        VERIFY_IS_TRUE(0 == memcmp(destination, body.data(), 8));

        VERIFY_SUCCEEDED(reader(call.handle, 8, sizeof(destination), readerContext, destination, &written));
        VERIFY_ARE_EQUAL(2u, written);
        VERIFY_IS_TRUE(0 == memcmp(destination, body.data() + 8, 2));

        VERIFY_SUCCEEDED(reader(call.handle, 10, sizeof(destination), readerContext, destination, &written));
        VERIFY_ARE_EQUAL(0u, written);

        VERIFY_ARE_EQUAL(E_FAIL, reader(call.handle, 11, sizeof(destination), readerContext, destination, &written));
        VERIFY_ARE_EQUAL(E_INVALIDARG, reader(call.handle, 0, 0, readerContext, destination, &written));
        VERIFY_ARE_EQUAL(E_INVALIDARG, reader(call.handle, 0, sizeof(destination), readerContext, nullptr, &written));
        VERIFY_ARE_EQUAL(E_INVALIDARG, reader(call.handle, 0, sizeof(destination), readerContext, destination, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, reader(nullptr, 0, sizeof(destination), readerContext, destination, &written));

        // Built-in writer appends to the response body buffer.
        const Bytes first = ToBytes("abc");
        const Bytes second = ToBytes("def");
        VERIFY_SUCCEEDED(writer(call.handle, first.data(), first.size(), writerContext));
        VERIFY_SUCCEEDED(writer(call.handle, second.data(), second.size(), writerContext));
        const char* responseString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("abcdef", responseString);
    }

    DEFINE_TEST_CASE(TestCustomRequestBodyReaderCalledInChunks)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCustomRequestBodyReaderCalledInChunks);

        FakeServer server;
        server.readChunkSize = 16;
        server.responseBody = ToBytes("done");
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "PUT", "https://example.test/blob"));

        ReaderState reader;
        reader.expectedCall = call.handle;
        reader.maxPerRead = 10; // the reader may return fewer bytes than offered
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyReadFunction(call.handle, PatternReader, 50, &reader));

        HCHttpCallRequestBodyReadFunction gotReader{ nullptr };
        size_t gotSize{ 0 };
        void* gotContext{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyReadFunction(call.handle, &gotReader, &gotSize, &gotContext));
        VERIFY_IS_TRUE(gotReader == &PatternReader);
        VERIFY_ARE_EQUAL(50u, gotSize);
        VERIFY_IS_TRUE(gotContext == &reader);

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        // The provider saw the client's reader, size and context, and the reader was pulled until the
        // declared size was reached.
        VERIFY_IS_TRUE(server.readFunction == &PatternReader);
        VERIFY_IS_TRUE(server.readContext == &reader);
        VERIFY_ARE_EQUAL(50u, server.readBodySize);
        VERIFY_IS_FALSE(reader.sawUnexpectedCall);
        VERIFY_ARE_EQUAL(5u, reader.reads);
        const std::vector<size_t> expectedOffsets{ 0, 10, 20, 30, 40 };
        const std::vector<size_t> expectedAvailable{ 16, 16, 16, 16, 10 };
        VERIFY_IS_TRUE(reader.offsets == expectedOffsets);
        VERIFY_IS_TRUE(reader.available == expectedAvailable);

        Bytes expectedBody(50);
        for (size_t i = 0; i < expectedBody.size(); ++i)
        {
            expectedBody[i] = PatternByte(i);
        }
        VERIFY_IS_TRUE(server.requestBody == expectedBody);

        // With a custom reader there is no buffered body: the size is reported but there are no bytes.
        VERIFY_ARE_EQUAL(50u, server.bodyBytesSize);
        VERIFY_IS_NULL(server.bodyBytesPointer);

        const char* responseString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("done", responseString);
    }

    DEFINE_TEST_CASE(TestCustomRequestBodyReaderErrorFailsCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCustomRequestBodyReaderErrorFailsCall);

        FakeServer server;
        server.readChunkSize = 8;
        server.responseBody = ToBytes("never sent");
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/upload"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.handle, true));

        ReaderState reader;
        reader.expectedCall = call.handle;
        reader.failOnRead = 2;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyReadFunction(call.handle, PatternReader, 32, &reader));

        WriterState writer;
        writer.expectedCall = call.handle;
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));

        // The reader's HRESULT is what the client sees, and a failed perform is not retried.
        VERIFY_ARE_EQUAL(c_readerFailure, PerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(2u, reader.reads);
        VERIFY_ARE_EQUAL(1u, server.performCount);
        VERIFY_ARE_EQUAL(0u, writer.writes);

        uint32_t performCount{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallGetPerformCount(call.handle, &performCount));
        VERIFY_ARE_EQUAL(1u, performCount);

        uint32_t statusCode{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetStatusCode(call.handle, &statusCode));
        VERIFY_ARE_EQUAL(0u, statusCode);
    }

    DEFINE_TEST_CASE(TestRequestBodyBytesAndReaderAreMutuallyExclusive)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestBodyBytesAndReaderAreMutuallyExclusive);

        ProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));

        int readerContext{ 0 };
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyReadFunction(call.handle, nullptr, 5, &readerContext));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRequestBodyReadFunction(nullptr, UnusedReader, 5, &readerContext));

        // Reader first, then bytes: the bytes win and the built-in reader is restored.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyReadFunction(call.handle, UnusedReader, 5, &readerContext));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyString(call.handle, "abc"));

        HCHttpCallRequestBodyReadFunction reader{ nullptr };
        size_t size{ 0 };
        void* context{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyReadFunction(call.handle, &reader, &size, &context));
        VERIFY_IS_TRUE(reader == &HC_CALL::ReadRequestBody);
        VERIFY_ARE_EQUAL(3u, size);
        VERIFY_IS_NULL(context);

        // Bytes first, then reader: the buffered body is dropped.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyReadFunction(call.handle, UnusedReader, 9, &readerContext));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyReadFunction(call.handle, &reader, &size, &context));
        VERIFY_IS_TRUE(reader == &UnusedReader);
        VERIFY_ARE_EQUAL(9u, size);
        VERIFY_IS_TRUE(context == &readerContext);

        const uint8_t* bodyBytes{ nullptr };
        uint32_t bodySize{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyBytes(call.handle, &bodyBytes, &bodySize));
        VERIFY_ARE_EQUAL(9u, bodySize);
        VERIFY_IS_NULL(bodyBytes);

        const char* bodyString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyString(call.handle, &bodyString));
        VERIFY_ARE_EQUAL_STR("", bodyString);

        // Custom response writer makes the buffered response APIs unavailable, on both sides.
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, nullptr, nullptr));
        WriterState writer;
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));

        HCHttpCallResponseBodyWriteFunction gotWriter{ nullptr };
        void* gotWriterContext{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyWriteFunction(call.handle, &gotWriter, &gotWriterContext));
        VERIFY_IS_TRUE(gotWriter == &CollectingWriter);
        VERIFY_IS_TRUE(gotWriterContext == &writer);

        const Bytes bytes = ToBytes("xyz");
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseSetResponseBodyBytes(call.handle, bytes.data(), bytes.size()));
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseAppendResponseBodyBytes(call.handle, bytes.data(), bytes.size()));

        size_t responseSize{ 0 };
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseGetResponseBodyBytesSize(call.handle, &responseSize));
        Bytes buffer(16);
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseGetResponseBodyBytes(call.handle, buffer.size(), buffer.data(), nullptr));
        const char* responseString{ nullptr };
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseGetResponseString(call.handle, &responseString));
    }

    DEFINE_TEST_CASE(TestCustomResponseWriterReceivesAllBytes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCustomResponseWriterReceivesAllBytes);

        FakeServer server;
        server.responseBody = MakePattern(23);
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/download"));

        WriterState writer;
        writer.expectedCall = call.handle;
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        VERIFY_IS_TRUE(server.writeFunction == &CollectingWriter);
        VERIFY_IS_TRUE(server.writeContext == &writer);
        VERIFY_ARE_EQUAL(E_FAIL, server.appendProbeHr);

        VERIFY_IS_FALSE(writer.sawUnexpectedCall);
        const std::vector<size_t> expectedSizes{ 5, 5, 5, 5, 3 };
        VERIFY_IS_TRUE(writer.sizes == expectedSizes);
        VERIFY_IS_TRUE(writer.received == server.responseBody);

        // The body went to the writer, not the call's buffer.
        size_t responseSize{ 0 };
        VERIFY_ARE_EQUAL(E_FAIL, HCHttpCallResponseGetResponseBodyBytesSize(call.handle, &responseSize));
        uint32_t statusCode{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetStatusCode(call.handle, &statusCode));
        VERIFY_ARE_EQUAL(200u, statusCode);
    }

    DEFINE_TEST_CASE(TestCustomResponseWriterErrorFailsCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCustomResponseWriterErrorFailsCall);

        FakeServer server;
        server.responseBody = MakePattern(30);
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/download"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.handle, true));

        WriterState writer;
        writer.expectedCall = call.handle;
        writer.failOnWrite = 2;
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));

        VERIFY_ARE_EQUAL(c_writerFailure, PerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(2u, writer.writes);
        VERIFY_ARE_EQUAL(2u, server.writeCount);
        VERIFY_ARE_EQUAL(5u, writer.received.size());
        VERIFY_ARE_EQUAL(1u, server.performCount);

        uint32_t performCount{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallGetPerformCount(call.handle, &performCount));
        VERIFY_ARE_EQUAL(1u, performCount);
    }

    DEFINE_TEST_CASE(TestProviderSetAndAppendResponseBodyBytes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProviderSetAndAppendResponseBodyBytes);

        FakeServer server;
        server.responseBody = ToBytes("hello world");
        server.respondWithSetAndAppend = true;
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        const char* responseString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("hello world", responseString);
        size_t responseSize{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseBodyBytesSize(call.handle, &responseSize));
        VERIFY_ARE_EQUAL(11u, responseSize);

        // Set replaces, Append concatenates, and both invalidate the cached response string.
        ScopedCall direct;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&direct.handle));
        const Bytes abc = ToBytes("abc");
        const Bytes def = ToBytes("def");
        const Bytes x = ToBytes("x");
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyBytes(direct.handle, abc.data(), abc.size()));
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(direct.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("abc", responseString);
        VERIFY_SUCCEEDED(HCHttpCallResponseAppendResponseBodyBytes(direct.handle, def.data(), def.size()));
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(direct.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("abcdef", responseString);
        VERIFY_SUCCEEDED(HCHttpCallResponseAppendResponseBodyBytes(direct.handle, def.data(), 0));
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(direct.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("abcdef", responseString);
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyBytes(direct.handle, x.data(), x.size()));
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(direct.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("x", responseString);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseAppendResponseBodyBytes(direct.handle, nullptr, 1));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseAppendResponseBodyBytes(nullptr, x.data(), x.size()));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetResponseBodyBytes(direct.handle, nullptr, 1));
    }

    DEFINE_TEST_CASE(TestRequestDynamicSizeDrivesUploadProgress)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestDynamicSizeDrivesUploadProgress);

        FakeServer server;
        server.readChunkSize = 16;
        server.responseBody = ToBytes("ok");
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        // Argument checks and "dynamic size must be set first".
        ScopedCall plain;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&plain.handle));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetDynamicSize(plain.handle, 0));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetDynamicSize(nullptr, 10));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCHttpCallRequestAddDynamicBytesWritten(plain.handle, 1));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestAddDynamicBytesWritten(nullptr, 1));
        size_t dynamicSize{ 99 };
        size_t dynamicWritten{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetDynamicBytesWritten(plain.handle, &dynamicSize, &dynamicWritten));
        VERIFY_ARE_EQUAL(0u, dynamicSize);
        VERIFY_ARE_EQUAL(0u, dynamicWritten);
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetDynamicBytesWritten(plain.handle, nullptr, &dynamicWritten));

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/stream"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetDynamicSize(call.handle, 100));

        // The reader reports 30 "dynamic" bytes per read; four reads overshoot and are clamped to 100.
        ReaderState reader;
        reader.expectedCall = call.handle;
        reader.maxPerRead = 10;
        reader.dynamicBytesPerRead = 30;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyReadFunction(call.handle, PatternReader, 40, &reader));

        ProgressRecorder upload;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 1, &upload));

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(S_OK, reader.addDynamicHr);
        VERIFY_ARE_EQUAL(4u, reader.reads);

        VERIFY_SUCCEEDED(HCHttpCallRequestGetDynamicBytesWritten(call.handle, &dynamicSize, &dynamicWritten));
        VERIFY_ARE_EQUAL(100u, dynamicSize);
        VERIFY_ARE_EQUAL(100u, dynamicWritten);

        // Progress uses the dynamic numbers rather than the 40-byte body size.
        VERIFY_ARE_EQUAL(4u, upload.records.size());
        const uint64_t expectedCurrent[] = { 30, 60, 90, 100 };
        for (size_t i = 0; i < upload.records.size(); ++i)
        {
            VERIFY_IS_TRUE(upload.records[i].call == call.handle);
            VERIFY_ARE_EQUAL(expectedCurrent[i], upload.records[i].current);
            VERIFY_ARE_EQUAL(100u, upload.records[i].total);
        }

        // Dynamic size can't change once the call is performed; bytes written may still be added.
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetDynamicSize(call.handle, 500));
        VERIFY_SUCCEEDED(HCHttpCallRequestAddDynamicBytesWritten(call.handle, 1));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetDynamicBytesWritten(call.handle, &dynamicSize, &dynamicWritten));
        VERIFY_ARE_EQUAL(100u, dynamicSize);
        VERIFY_ARE_EQUAL(100u, dynamicWritten);
    }

    DEFINE_TEST_CASE(TestResponseDynamicSizeDrivesDownloadProgress)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseDynamicSizeDrivesDownloadProgress);

        FakeServer server;
        server.responseBody = MakePattern(15);
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall plain;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&plain.handle));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetDynamicSize(plain.handle, 0));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetDynamicSize(nullptr, 10));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCHttpCallResponseAddDynamicBytesWritten(plain.handle, 1));
        VERIFY_SUCCEEDED(HCHttpCallResponseSetDynamicSize(plain.handle, 100));
        VERIFY_SUCCEEDED(HCHttpCallResponseAddDynamicBytesWritten(plain.handle, 60));
        VERIFY_SUCCEEDED(HCHttpCallResponseAddDynamicBytesWritten(plain.handle, 60));
        size_t dynamicSize{ 0 };
        size_t dynamicWritten{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetDynamicBytesWritten(plain.handle, &dynamicSize, &dynamicWritten));
        VERIFY_ARE_EQUAL(100u, dynamicSize);
        VERIFY_ARE_EQUAL(100u, dynamicWritten); // clamped
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseGetDynamicBytesWritten(plain.handle, &dynamicSize, nullptr));

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/stream"));
        VERIFY_SUCCEEDED(HCHttpCallResponseSetDynamicSize(call.handle, 1000));

        WriterState writer;
        writer.expectedCall = call.handle;
        writer.dynamicBytesPerWrite = 250;
        VERIFY_SUCCEEDED(HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));

        ProgressRecorder download;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, false, 1, &download));

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(S_OK, writer.addDynamicHr);
        VERIFY_ARE_EQUAL(3u, writer.writes);
        VERIFY_IS_TRUE(writer.received == server.responseBody);

        // Providers can't set the response dynamic size during perform.
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, server.responseSetDynamicSizeHr);

        VERIFY_SUCCEEDED(HCHttpCallResponseGetDynamicBytesWritten(call.handle, &dynamicSize, &dynamicWritten));
        VERIFY_ARE_EQUAL(1000u, dynamicSize);
        VERIFY_ARE_EQUAL(750u, dynamicWritten);

        VERIFY_ARE_EQUAL(3u, download.records.size());
        for (size_t i = 0; i < download.records.size(); ++i)
        {
            VERIFY_ARE_EQUAL(250u * (i + 1), download.records[i].current);
            VERIFY_ARE_EQUAL(1000u, download.records[i].total);
        }
    }

    DEFINE_TEST_CASE(TestProgressReportFunctionRegistration)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProgressReportFunctionRegistration);

        ProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));

        // Defaults: no function, 2 second interval, no context, for both directions.
        for (bool isUpload : { true, false })
        {
            HCHttpCallProgressReportFunction function{ RecordProgress };
            size_t interval{ 0 };
            void* context{ &interval };
            VERIFY_SUCCEEDED(HCHttpCallRequestGetProgressReportFunction(call.handle, isUpload, &function, &interval, &context));
            VERIFY_IS_TRUE(function == nullptr);
            VERIFY_ARE_EQUAL(2u, interval);
            VERIFY_IS_NULL(context);
        }

        ProgressRecorder upload;
        ProgressRecorder download;
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetProgressReportFunction(call.handle, nullptr, true, 1, &upload));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 0, &upload));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetProgressReportFunction(nullptr, RecordProgress, true, 1, &upload));

        // Upload and download registrations are independent.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 1, &upload));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, false, 7, &download));

        HCHttpCallProgressReportFunction function{ nullptr };
        size_t interval{ 0 };
        void* context{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetProgressReportFunction(call.handle, true, &function, &interval, &context));
        VERIFY_IS_TRUE(function == &RecordProgress);
        VERIFY_ARE_EQUAL(1u, interval);
        VERIFY_IS_TRUE(context == &upload);
        VERIFY_SUCCEEDED(HCHttpCallRequestGetProgressReportFunction(call.handle, false, &function, &interval, &context));
        VERIFY_IS_TRUE(function == &RecordProgress);
        VERIFY_ARE_EQUAL(7u, interval);
        VERIFY_IS_TRUE(context == &download);

        // Re-registering replaces the previous registration for that direction only.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 3, nullptr));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetProgressReportFunction(call.handle, true, &function, &interval, &context));
        VERIFY_ARE_EQUAL(3u, interval);
        VERIFY_IS_NULL(context);
        VERIFY_SUCCEEDED(HCHttpCallRequestGetProgressReportFunction(call.handle, false, &function, &interval, &context));
        VERIFY_ARE_EQUAL(7u, interval);
        VERIFY_IS_TRUE(context == &download);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call.handle, true, nullptr, &interval, &context));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call.handle, true, &function, nullptr, &context));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call.handle, true, &function, &interval, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(nullptr, true, &function, &interval, &context));
    }

    DEFINE_TEST_CASE(TestProgressReportsDuringPerform)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProgressReportsDuringPerform);

        FakeServer server;
        server.responseBody = MakePattern(12);
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/sync"));
        const Bytes body = MakePattern(30);
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyBytes(call.handle, body.data(), static_cast<uint32_t>(body.size())));

        ProgressRecorder upload;
        ProgressRecorder download;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 1, &upload));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, false, 4, &download));

        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        // The provider sees the configured intervals (in seconds) for each direction.
        VERIFY_ARE_EQUAL(1u, server.uploadInterval);
        VERIFY_ARE_EQUAL(4u, server.downloadInterval);

        const uint64_t expectedUpload[] = { 7, 14, 21, 28, 30 };
        VERIFY_ARE_EQUAL(5u, upload.records.size());
        for (size_t i = 0; i < upload.records.size(); ++i)
        {
            VERIFY_IS_TRUE(upload.records[i].call == call.handle);
            VERIFY_ARE_EQUAL(expectedUpload[i], upload.records[i].current);
            VERIFY_ARE_EQUAL(30u, upload.records[i].total);
        }

        const uint64_t expectedDownload[] = { 5, 10, 12 };
        VERIFY_ARE_EQUAL(3u, download.records.size());
        for (size_t i = 0; i < download.records.size(); ++i)
        {
            VERIFY_IS_TRUE(download.records[i].call == call.handle);
            VERIFY_ARE_EQUAL(expectedDownload[i], download.records[i].current);
            VERIFY_ARE_EQUAL(12u, download.records[i].total);
        }

        // Progress registration is frozen once the call is performed.
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetProgressReportFunction(call.handle, RecordProgress, true, 1, &upload));

        // A failing progress callback fails the call with its HRESULT.
        ScopedCall failing;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&failing.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(failing.handle, "GET", "https://example.test/sync"));
        ProgressRecorder failingDownload;
        failingDownload.returnHr = c_progressFailure;
        VERIFY_SUCCEEDED(HCHttpCallRequestSetProgressReportFunction(failing.handle, RecordProgress, false, 1, &failingDownload));
        VERIFY_ARE_EQUAL(c_progressFailure, PerformAndWait(failing.handle));
        VERIFY_ARE_EQUAL(1u, failingDownload.records.size());
    }

    DEFINE_TEST_CASE(TestBodyConfigurationRejectedAfterPerform)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestBodyConfigurationRejectedAfterPerform);

        FakeServer server;
        server.responseBody = ToBytes("ok");
        ProviderScope scope{ FakeServerPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        ScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyString(call.handle, "abc"));
        VERIFY_SUCCEEDED(PerformAndWait(call.handle));

        const uint8_t byte = 1;
        int context{ 0 };
        WriterState writer;
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRequestBodyBytes(call.handle, &byte, 1));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRequestBodyString(call.handle, "def"));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRequestBodyReadFunction(call.handle, UnusedReader, 1, &context));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallResponseSetResponseBodyWriteFunction(call.handle, CollectingWriter, &writer));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetDynamicSize(call.handle, 10));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallResponseSetDynamicSize(call.handle, 10));

        // Nothing changed.
        const char* bodyString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRequestBodyString(call.handle, &bodyString));
        VERIFY_ARE_EQUAL_STR("abc", bodyString);
        const char* responseString{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.handle, &responseString));
        VERIFY_ARE_EQUAL_STR("ok", responseString);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
