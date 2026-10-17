// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "Logger/trace_internal.h"
#include "XAsync.h"
#include "XAsyncProvider.h"
#include "XTaskQueue.h"

#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma warning(disable:4389)

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// A private trace area so these tests do not depend on (or disturb) the library's own areas.
HC_DEFINE_TRACE_AREA(TRACEAPITEST, HCTraceLevel::Verbose);

constexpr char c_testAreaName[] = "TRACEAPITEST";
constexpr char c_messageTag[] = "TraceApiTests";
constexpr size_t c_traceMessageBufferSize = 4096; // trace.cpp: char message[4096]

struct CapturedTrace
{
    std::string area;
    HCTraceLevel level;
    uint64_t threadId;
    uint64_t timestamp;
    std::string message;
};

std::mutex g_capturedTracesMutex;
std::vector<CapturedTrace> g_capturedTraces;

// Only keeps traces from the test area, or library-area traces carrying the test tag, so unrelated library
// traces emitted concurrently on other threads cannot affect the assertions.
void CALLBACK CaptureTrace(
    _In_z_ const char* areaName,
    _In_ HCTraceLevel level,
    _In_ uint64_t threadId,
    _In_ uint64_t timestamp,
    _In_z_ const char* message) noexcept
{
    if (strcmp(areaName, c_testAreaName) != 0 && strstr(message, c_messageTag) == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock{ g_capturedTracesMutex };
    g_capturedTraces.push_back(CapturedTrace{ areaName, level, threadId, timestamp, message });
}

std::vector<CapturedTrace> TakeCapturedTraces()
{
    std::lock_guard<std::mutex> lock{ g_capturedTracesMutex };
    std::vector<CapturedTrace> traces;
    traces.swap(g_capturedTraces);
    return traces;
}

void EmitTraceV(HCTraceImplArea const* area, HCTraceLevel level, _Printf_format_string_ char const* format, ...)
{
    va_list args;
    va_start(args, format);
    HCTraceImplMessage_v(area, level, format, args);
    va_end(args);
}

uint64_t CurrentThreadIdForTrace()
{
#if HC_PLATFORM_IS_MICROSOFT
    return GetCurrentThreadId();
#else
    return 0;
#endif
}

// Initializes tracing with the capture callback and restores every global it touches on exit.
class TraceApiScope
{
public:
    TraceApiScope() noexcept :
        m_httpClientVerbosity{ HC_TRACE_GET_VERBOSITY(HTTPCLIENT) },
        m_webSocketVerbosity{ HC_TRACE_GET_VERBOSITY(WEBSOCKET) },
        m_traceToDebugger{ GetTraceState().GetTraceToDebugger() },
        m_etwEnabled{ GetTraceState().GetEtwEnabled() }
    {
        HCTraceInit();
        HC_TRACE_SET_VERBOSITY(TRACEAPITEST, HCTraceLevel::Verbose);
        TakeCapturedTraces();
        m_callbackAdded = HCTraceSetClientCallback(&CaptureTrace);
    }

    ~TraceApiScope()
    {
        HCTraceRemoveClientCallback(&CaptureTrace);
        HCTraceCleanup();
        HC_TRACE_SET_VERBOSITY(HTTPCLIENT, m_httpClientVerbosity);
        HC_TRACE_SET_VERBOSITY(WEBSOCKET, m_webSocketVerbosity);
        HC_TRACE_SET_VERBOSITY(TRACEAPITEST, HCTraceLevel::Verbose);
        HCTraceSetTraceToDebugger(m_traceToDebugger);
#if HC_PLATFORM_IS_MICROSOFT
        HCTraceSetEtwEnabled(m_etwEnabled);
#endif
        TakeCapturedTraces();
    }

    TraceApiScope(TraceApiScope const&) = delete;
    TraceApiScope& operator=(TraceApiScope const&) = delete;

    void VerifyInitialized() const
    {
        VERIFY_IS_TRUE(m_callbackAdded);
    }

private:
    HCTraceLevel const m_httpClientVerbosity;
    HCTraceLevel const m_webSocketVerbosity;
    bool const m_traceToDebugger;
    bool const m_etwEnabled;
    bool m_callbackAdded{ false };
};

//------------------------------------------------------------------------------
// XAsync provider with a result payload
//------------------------------------------------------------------------------

struct PayloadOperation
{
    std::vector<uint8_t> payload;
    HRESULT completionResult{ S_OK };
};

int g_payloadOperationIdentity{ 0 };

HRESULT CALLBACK PayloadOperationProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    auto operation = static_cast<PayloadOperation*>(data->context);
    switch (op)
    {
    case XAsyncOp::Begin:
        return XAsyncSchedule(data->async, 0);

    case XAsyncOp::DoWork:
        XAsyncComplete(data->async, operation->completionResult, SUCCEEDED(operation->completionResult) ? operation->payload.size() : 0);
        return S_OK;

    case XAsyncOp::GetResult:
        memcpy(data->buffer, operation->payload.data(), operation->payload.size());
        return S_OK;

    default:
        return S_OK;
    }
}

void DrainQueue(XTaskQueueHandle queue)
{
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0) || XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
    {
    }
}

class ManualQueue
{
public:
    ManualQueue() noexcept
    {
        m_createResult = XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &m_queue);
    }

    ~ManualQueue()
    {
        if (m_queue != nullptr)
        {
            DrainQueue(m_queue);
            XTaskQueueCloseHandle(m_queue);
        }
    }

    ManualQueue(ManualQueue const&) = delete;
    ManualQueue& operator=(ManualQueue const&) = delete;

    XTaskQueueHandle Get() const
    {
        VERIFY_ARE_EQUAL(S_OK, m_createResult);
        return m_queue;
    }

private:
    HRESULT m_createResult{ E_FAIL };
    XTaskQueueHandle m_queue{ nullptr };
};

} // namespace

DEFINE_TEST_CLASS(TraceApiTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(TraceApiTests);

    DEFINE_TEST_CASE(TestImplMessageReachesClientCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestImplMessageReachesClientCallback);
        TraceApiScope scope;
        scope.VerifyInitialized();

        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Important, "TraceApiTests %s-%d-%04x", "fmt", 42, 0xBEEF);
        EmitTraceV(&g_traceTRACEAPITEST, HCTraceLevel::Warning, "via _v: %u %s", 7u, "args");
        HC_TRACE_MESSAGE(TRACEAPITEST, HCTraceLevel::Error, "macro %d%%", 100);

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(3u, static_cast<uint32_t>(traces.size()));

        VERIFY_ARE_EQUAL_STR(c_testAreaName, traces[0].area.c_str());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Important), static_cast<uint32_t>(traces[0].level));
        VERIFY_ARE_EQUAL_STR("TraceApiTests fmt-42-beef", traces[0].message.c_str());

        VERIFY_ARE_EQUAL_STR(c_testAreaName, traces[1].area.c_str());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Warning), static_cast<uint32_t>(traces[1].level));
        VERIFY_ARE_EQUAL_STR("via _v: 7 args", traces[1].message.c_str());

        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Error), static_cast<uint32_t>(traces[2].level));
        VERIFY_ARE_EQUAL_STR("macro 100%", traces[2].message.c_str());

        // Callbacks run synchronously on the tracing thread; timestamps never go backwards.
        VERIFY_IS_TRUE(traces[0].threadId == traces[1].threadId);
        VERIFY_IS_TRUE(traces[1].threadId == traces[2].threadId);
#if HC_PLATFORM_IS_MICROSOFT
        VERIFY_IS_TRUE(traces[0].threadId == CurrentThreadIdForTrace());
#endif
        VERIFY_IS_TRUE(traces[0].timestamp <= traces[1].timestamp);
        VERIFY_IS_TRUE(traces[1].timestamp <= traces[2].timestamp);
    }

    DEFINE_TEST_CASE(TestAreaVerbosityFiltersLevels)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestAreaVerbosityFiltersLevels);
        TraceApiScope scope;
        scope.VerifyInitialized();

        HCTraceLevel const levels[] =
        {
            HCTraceLevel::Error,
            HCTraceLevel::Warning,
            HCTraceLevel::Important,
            HCTraceLevel::Information,
            HCTraceLevel::Verbose,
        };
        HCTraceLevel const verbosities[] =
        {
            HCTraceLevel::Off,
            HCTraceLevel::Error,
            HCTraceLevel::Warning,
            HCTraceLevel::Important,
            HCTraceLevel::Information,
            HCTraceLevel::Verbose,
        };

        for (auto verbosity : verbosities)
        {
            HC_TRACE_SET_VERBOSITY(TRACEAPITEST, verbosity);
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(verbosity), static_cast<uint32_t>(HC_TRACE_GET_VERBOSITY(TRACEAPITEST)));

            for (auto level : levels)
            {
                HCTraceImplMessage(&g_traceTRACEAPITEST, level, "level %u", static_cast<uint32_t>(level));
            }

            // A message is delivered when its level is at or below the area's verbosity.
            auto traces = TakeCapturedTraces();
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(verbosity), static_cast<uint32_t>(traces.size()));
            for (size_t i = 0; i < traces.size(); ++i)
            {
                VERIFY_ARE_EQUAL(static_cast<uint32_t>(levels[i]), static_cast<uint32_t>(traces[i].level));
            }
        }
    }

    DEFINE_TEST_CASE(TestSettingsTraceLevelFiltersLibraryAreas)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettingsTraceLevelFiltersLibraryAreas);
        TraceApiScope scope;
        scope.VerifyInitialized();

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCSettingsGetTraceLevel(nullptr));

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Warning));
        HCTraceLevel level = HCTraceLevel::Verbose;
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Warning), static_cast<uint32_t>(level));
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Warning), static_cast<uint32_t>(HC_TRACE_GET_VERBOSITY(WEBSOCKET)));

        HC_TRACE_INFORMATION(HTTPCLIENT, "TraceApiTests http info");
        HC_TRACE_WARNING(HTTPCLIENT, "TraceApiTests http warning");
        HC_TRACE_INFORMATION(WEBSOCKET, "TraceApiTests ws info");
        HC_TRACE_ERROR(WEBSOCKET, "TraceApiTests ws error");
        // The library level does not affect other areas.
        HC_TRACE_VERBOSE(TRACEAPITEST, "TraceApiTests own verbose");

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(3u, static_cast<uint32_t>(traces.size()));
        VERIFY_ARE_EQUAL_STR("HTTPCLIENT", traces[0].area.c_str());
        VERIFY_ARE_EQUAL_STR("TraceApiTests http warning", traces[0].message.c_str());
        VERIFY_ARE_EQUAL_STR("WEBSOCKET", traces[1].area.c_str());
        VERIFY_ARE_EQUAL_STR("TraceApiTests ws error", traces[1].message.c_str());
        VERIFY_ARE_EQUAL_STR(c_testAreaName, traces[2].area.c_str());

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Off));
        HC_TRACE_ERROR(HTTPCLIENT, "TraceApiTests http error while off");
        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(TakeCapturedTraces().size()));

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Verbose));
        HC_TRACE_VERBOSE(HTTPCLIENT, "TraceApiTests http verbose");
        traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(1u, static_cast<uint32_t>(traces.size()));
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCTraceLevel::Verbose), static_cast<uint32_t>(traces[0].level));
    }

    DEFINE_TEST_CASE(TestLongMessagesAreTruncated)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestLongMessagesAreTruncated);
        TraceApiScope scope;
        scope.VerifyInitialized();

        size_t const maxLength = c_traceMessageBufferSize - 1;

        std::string exact(maxLength, 'e');
        std::string longer(maxLength + 1, 'l');
        std::string huge(5 * c_traceMessageBufferSize, 'h');
        for (size_t i = 0; i < huge.size(); i += 97)
        {
            huge[i] = static_cast<char>('a' + (i % 26));
        }

        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "%s", exact.c_str());
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "%s", longer.c_str());
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "%s", huge.c_str());

        // Also run the debugger (and ETW) formatting paths, which add a prefix to an already full buffer.
        HCTraceSetTraceToDebugger(true);
#if HC_PLATFORM_IS_MICROSOFT
        HCTraceSetEtwEnabled(true);
#endif
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "%s", huge.c_str());
        HCTraceSetTraceToDebugger(false);
#if HC_PLATFORM_IS_MICROSOFT
        HCTraceSetEtwEnabled(false);
#endif

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(4u, static_cast<uint32_t>(traces.size()));
        VERIFY_IS_TRUE(traces[0].message == exact);
        VERIFY_ARE_EQUAL(maxLength, traces[1].message.size());
        VERIFY_IS_TRUE(traces[1].message == longer.substr(0, maxLength));
        VERIFY_ARE_EQUAL(maxLength, traces[2].message.size());
        VERIFY_IS_TRUE(traces[2].message == huge.substr(0, maxLength));
        VERIFY_IS_TRUE(traces[3].message == traces[2].message);
    }

    DEFINE_TEST_CASE(TestNullAreaOrFormatIsIgnored)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNullAreaOrFormatIsIgnored);
        TraceApiScope scope;
        scope.VerifyInitialized();

        char const* nullFormat = nullptr;
        HCTraceImplMessage(nullptr, HCTraceLevel::Error, "TraceApiTests no area");
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, nullFormat);
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "");

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(1u, static_cast<uint32_t>(traces.size()));
        VERIFY_ARE_EQUAL_STR("", traces[0].message.c_str());
    }

    DEFINE_TEST_CASE(TestTraceInitAndCleanupGateDelivery)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestTraceInitAndCleanupGateDelivery);
        HC_TRACE_SET_VERBOSITY(TRACEAPITEST, HCTraceLevel::Verbose);
        TakeCapturedTraces();

        // Without an outstanding HCTraceInit nothing is delivered, even with a callback registered. (Skipped if a
        // previous test leaked an initialization.)
        if (!GetTraceState().IsSetup())
        {
            VERIFY_IS_TRUE(HCTraceSetClientCallback(&CaptureTrace));
            HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "before init");
            VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(TakeCapturedTraces().size()));
            HCTraceRemoveClientCallback(&CaptureTrace);
        }

        // Init is reference counted, but every HCTraceCleanup clears all client callbacks.
        HCTraceInit();
        HCTraceInit();
        VERIFY_IS_TRUE(GetTraceState().IsSetup());
        VERIFY_IS_TRUE(HCTraceSetClientCallback(&CaptureTrace));
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "while initialized");
        VERIFY_ARE_EQUAL(1u, static_cast<uint32_t>(TakeCapturedTraces().size()));

        HCTraceCleanup();
        VERIFY_IS_TRUE(GetTraceState().IsSetup());
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "after one cleanup");
        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(TakeCapturedTraces().size()));

        VERIFY_IS_TRUE(HCTraceSetClientCallback(&CaptureTrace));
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "callback re-added");
        VERIFY_ARE_EQUAL(1u, static_cast<uint32_t>(TakeCapturedTraces().size()));

        HCTraceCleanup();
        HCTraceRemoveClientCallback(&CaptureTrace);
        TakeCapturedTraces();
    }

    DEFINE_TEST_CASE(TestSetPlatformCallbacks)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSetPlatformCallbacks);

        struct PlatformCallbackRecord
        {
            static uint64_t CALLBACK ThreadId(void* context)
            {
                ++static_cast<PlatformCallbackRecord*>(context)->threadIdCalls;
                return 0x1234;
            }

            static void CALLBACK WriteToDebugger(char const*, HCTraceLevel, char const*, void* context)
            {
                ++static_cast<PlatformCallbackRecord*>(context)->writeCalls;
            }

            uint32_t threadIdCalls{ 0 };
            uint32_t writeCalls{ 0 };
        } record;

        // Platform callbacks can only be changed while tracing is not initialized.
        HCTraceInit();
        VERIFY_ARE_EQUAL(E_HC_ALREADY_INITIALISED, HCTraceSetPlatformCallbacks(&PlatformCallbackRecord::ThreadId, &record, &PlatformCallbackRecord::WriteToDebugger, &record));
        HCTraceCleanup();

        if (GetTraceState().IsSetup())
        {
            return; // a previous test leaked an initialization; the rest needs tracing torn down
        }

        VERIFY_ARE_EQUAL(S_OK, HCTraceSetPlatformCallbacks(&PlatformCallbackRecord::ThreadId, &record, &PlatformCallbackRecord::WriteToDebugger, &record));
        {
            TraceApiScope scope;
            scope.VerifyInitialized();
            HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "platform callbacks");
            auto traces = TakeCapturedTraces();
            VERIFY_ARE_EQUAL(1u, static_cast<uint32_t>(traces.size()));
#if HC_PLATFORM_IS_MICROSOFT
            // The Windows trace platform always uses the OS thread id and OutputDebugString; the callbacks are
            // only consulted by the Generic trace platform.
            VERIFY_IS_TRUE(traces[0].threadId == CurrentThreadIdForTrace());
            VERIFY_ARE_EQUAL(0u, record.threadIdCalls);
#endif
        }

        // Restore the defaults.
        VERIFY_ARE_EQUAL(S_OK, HCTraceSetPlatformCallbacks(nullptr, nullptr, nullptr, nullptr));
    }

#if HC_PLATFORM_IS_MICROSOFT
    DEFINE_TEST_CASE(TestSetEtwEnabledIsSafe)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSetEtwEnabledIsSafe);
        TraceApiScope scope;
        scope.VerifyInitialized();

        // Enabling more than once must not re-register the provider; client callbacks keep working.
        HCTraceSetEtwEnabled(true);
        HCTraceSetEtwEnabled(true);
        VERIFY_IS_TRUE(GetTraceState().GetEtwEnabled());
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "with etw %d", 1);
        HCTraceSetEtwEnabled(false);
        HCTraceSetEtwEnabled(false);
        VERIFY_IS_FALSE(GetTraceState().GetEtwEnabled());
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "without etw %d", 2);
        HCTraceSetEtwEnabled(true);

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(2u, static_cast<uint32_t>(traces.size()));
        VERIFY_ARE_EQUAL_STR("with etw 1", traces[0].message.c_str());
        VERIFY_ARE_EQUAL_STR("without etw 2", traces[1].message.c_str());

        // With only ETW enabled (no client callbacks, no debugger) tracing still runs without a consumer.
        HCTraceRemoveClientCallback(&CaptureTrace);
        HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Error, "etw only");
        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(TakeCapturedTraces().size()));
    }
#endif

    DEFINE_TEST_CASE(TestScopeIdAndScopeHelper)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestScopeIdAndScopeHelper);
        TraceApiScope scope;
        scope.VerifyInitialized();

        uint64_t first = HCTraceImplScopeId();
        uint64_t second = HCTraceImplScopeId();
        VERIFY_IS_TRUE(second >= first);

        {
            HCTraceImplScopeHelper helper{ &g_traceTRACEAPITEST, HCTraceLevel::Information, "TraceApiTestsScope" };
            HCTraceImplMessage(&g_traceTRACEAPITEST, HCTraceLevel::Information, "inside");
        }

        auto traces = TakeCapturedTraces();
        VERIFY_ARE_EQUAL(3u, static_cast<uint32_t>(traces.size()));

        std::string const enterPrefix = ">>> TraceApiTestsScope (";
        std::string const exitPrefix = "<<< TraceApiTestsScope (";
        VERIFY_IS_TRUE(traces[0].message.compare(0, enterPrefix.size(), enterPrefix) == 0);
        VERIFY_ARE_EQUAL_STR("inside", traces[1].message.c_str());
        VERIFY_IS_TRUE(traces[2].message.compare(0, exitPrefix.size(), exitPrefix) == 0);

        // Both lines carry the same 16-hex-digit scope id.
        std::string const enterId = traces[0].message.substr(enterPrefix.size());
        std::string const exitId = traces[2].message.substr(exitPrefix.size());
        VERIFY_ARE_EQUAL(17u, static_cast<uint32_t>(enterId.size())); // 16 digits + ')'
        VERIFY_IS_TRUE(enterId == exitId);

        // A scope below the area's verbosity emits nothing.
        HC_TRACE_SET_VERBOSITY(TRACEAPITEST, HCTraceLevel::Warning);
        {
            HC_TRACE_SCOPE(TRACEAPITEST, HCTraceLevel::Verbose);
        }
        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(TakeCapturedTraces().size()));
    }
};

DEFINE_TEST_CLASS(AsyncResultSizeTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(AsyncResultSizeTests);

    DEFINE_TEST_CASE(TestResultSizeForCompletedPayload)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResultSizeForCompletedPayload);
        ManualQueue queue;

        PayloadOperation operation;
        operation.payload = { 0x00, 0x11, 0x22, 0x33, 0xFF, 0x00, 0x42 };

        XAsyncBlock asyncBlock{};
        asyncBlock.queue = queue.Get();
        VERIFY_ARE_EQUAL(S_OK, XAsyncBegin(&asyncBlock, &operation, &g_payloadOperationIdentity, __FUNCTION__, PayloadOperationProvider));

        // Not completed yet.
        size_t size = 12345;
        VERIFY_ARE_EQUAL(E_PENDING, XAsyncGetResultSize(&asyncBlock, &size));
        VERIFY_ARE_EQUAL(0u, size);

        DrainQueue(queue.Get());
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&asyncBlock, false));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &size));
        VERIFY_ARE_EQUAL(operation.payload.size(), size);

        // Asking again does not consume the result.
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &size));
        VERIFY_ARE_EQUAL(operation.payload.size(), size);

        std::vector<uint8_t> buffer(size + 4, 0xCC);
        size_t used = 0;
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetResult(&asyncBlock, &g_payloadOperationIdentity, buffer.size(), buffer.data(), &used));
        VERIFY_ARE_EQUAL(operation.payload.size(), used);
        VERIFY_IS_TRUE(memcmp(operation.payload.data(), buffer.data(), operation.payload.size()) == 0);

        // Once the result has been retrieved the payload is released.
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &size));
        VERIFY_ARE_EQUAL(0u, size);
        VERIFY_ARE_EQUAL(E_ILLEGAL_METHOD_CALL, XAsyncGetResult(&asyncBlock, &g_payloadOperationIdentity, buffer.size(), buffer.data(), &used));
    }

    DEFINE_TEST_CASE(TestResultSizeForFailedAndEmptyResults)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResultSizeForFailedAndEmptyResults);
        ManualQueue queue;

        // A failed operation reports its failure and no payload.
        {
            PayloadOperation operation;
            operation.payload = { 1, 2, 3 };
            operation.completionResult = E_ACCESSDENIED;

            XAsyncBlock asyncBlock{};
            asyncBlock.queue = queue.Get();
            VERIFY_ARE_EQUAL(S_OK, XAsyncBegin(&asyncBlock, &operation, &g_payloadOperationIdentity, __FUNCTION__, PayloadOperationProvider));
            DrainQueue(queue.Get());

            size_t size = 12345;
            VERIFY_ARE_EQUAL(E_ACCESSDENIED, XAsyncGetResultSize(&asyncBlock, &size));
            VERIFY_ARE_EQUAL(0u, size);
        }

        // A successful operation without a payload has a zero result size.
        {
            PayloadOperation operation;

            XAsyncBlock asyncBlock{};
            asyncBlock.queue = queue.Get();
            VERIFY_ARE_EQUAL(S_OK, XAsyncBegin(&asyncBlock, &operation, &g_payloadOperationIdentity, __FUNCTION__, PayloadOperationProvider));
            DrainQueue(queue.Get());

            size_t size = 12345;
            VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &size));
            VERIFY_ARE_EQUAL(0u, size);
        }

        // A buffer smaller than the result size is rejected.
        {
            PayloadOperation operation;
            operation.payload.assign(64, 0x5A);

            XAsyncBlock asyncBlock{};
            asyncBlock.queue = queue.Get();
            VERIFY_ARE_EQUAL(S_OK, XAsyncBegin(&asyncBlock, &operation, &g_payloadOperationIdentity, __FUNCTION__, PayloadOperationProvider));
            DrainQueue(queue.Get());

            size_t size = 0;
            VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &size));
            VERIFY_ARE_EQUAL(64u, size);

            std::vector<uint8_t> tooSmall(size - 1);
            VERIFY_ARE_EQUAL(E_NOT_SUFFICIENT_BUFFER, XAsyncGetResult(&asyncBlock, &g_payloadOperationIdentity, tooSmall.size(), tooSmall.data(), nullptr));
        }
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
