// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Request settings and globals (test-gap plan item 1.8): global defaults vs per-call overrides for
// timeout, timeout window, retry delay and retry allowed; per-call max receive buffer size, retry cache
// id and SSL validation; HCSetGlobalProxy and HCHttpDisableAssertsForSSLValidationInDevSandboxes.
//
// Calls are served by a fake provider (HCSetHttpCallPerformFunction) that records what it reads
// through the provider-side getters, so each test checks the value a provider would actually use.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "HTTP/httpcall.h"
#include "Platform/ExternalHttpProvider.h"
#include <httpClient/httpProvider.h>
#include <vector>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// Documented defaults (httpClient.h / Source/Global/global.h).
constexpr uint32_t c_defaultTimeoutSeconds = 30;
constexpr uint32_t c_defaultTimeoutWindowSeconds = 20;
constexpr uint32_t c_defaultRetryDelaySeconds = 2;
constexpr bool c_defaultRetryAllowed = true;

class SettingsProviderScope
{
public:
    SettingsProviderScope(HCCallPerformFunction perform, void* context) noexcept
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

    ~SettingsProviderScope()
    {
        HCCleanup();
        ExternalHttpProvider::Get().ClearCallback();
    }

    SettingsProviderScope(const SettingsProviderScope&) = delete;
    SettingsProviderScope& operator=(const SettingsProviderScope&) = delete;

    HRESULT InitHr() const noexcept { return m_initHr; }

private:
    HRESULT m_initHr{ S_OK };
};

// Snapshots the global (call == nullptr) request settings and restores them on destruction, so a
// failing test can't leak changed defaults into the next one. Must be destroyed before HCCleanup.
class GlobalSettingsRestorer
{
public:
    GlobalSettingsRestorer() noexcept
    {
        m_captured =
            SUCCEEDED(HCHttpCallRequestGetRetryAllowed(nullptr, &m_retryAllowed)) &&
            SUCCEEDED(HCHttpCallRequestGetTimeout(nullptr, &m_timeout)) &&
            SUCCEEDED(HCHttpCallRequestGetTimeoutWindow(nullptr, &m_timeoutWindow)) &&
            SUCCEEDED(HCHttpCallRequestGetRetryDelay(nullptr, &m_retryDelay));
    }

    ~GlobalSettingsRestorer()
    {
        if (m_captured)
        {
            HCHttpCallRequestSetRetryAllowed(nullptr, m_retryAllowed);
            HCHttpCallRequestSetTimeout(nullptr, m_timeout);
            HCHttpCallRequestSetTimeoutWindow(nullptr, m_timeoutWindow);
            HCHttpCallRequestSetRetryDelay(nullptr, m_retryDelay);
        }
    }

    GlobalSettingsRestorer(const GlobalSettingsRestorer&) = delete;
    GlobalSettingsRestorer& operator=(const GlobalSettingsRestorer&) = delete;

    bool Captured() const noexcept { return m_captured; }

private:
    bool m_captured{ false };
    bool m_retryAllowed{ true };
    uint32_t m_timeout{ 0 };
    uint32_t m_timeoutWindow{ 0 };
    uint32_t m_retryDelay{ 0 };
};

struct SettingsScopedCall
{
    SettingsScopedCall() = default;
    SettingsScopedCall(const SettingsScopedCall&) = delete;
    SettingsScopedCall& operator=(const SettingsScopedCall&) = delete;
    ~SettingsScopedCall()
    {
        if (handle != nullptr)
        {
            HCHttpCallCloseHandle(handle);
        }
    }

    HCCallHandle handle{ nullptr };
};

HRESULT SettingsPerformAndWait(HCCallHandle call)
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

struct ObservedSettings
{
    HRESULT hr{ E_PENDING };
    bool retryAllowed{ false };
    uint32_t timeout{ 0 };
    uint32_t timeoutWindow{ 0 };
    uint32_t retryDelay{ 0 };
    uint32_t retryCacheId{ 0 };
    size_t maxReceiveBufferSize{ 0 };
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
    bool sslValidation{ false };
#endif
};

struct SettingsServer
{
    std::vector<ObservedSettings> observed;
};

void CALLBACK SettingsPerform(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ HCPerformEnv /*env*/
)
{
    auto& server = *static_cast<SettingsServer*>(context);

    ObservedSettings seen;
    seen.hr = S_OK;
    if (FAILED(HCHttpCallRequestGetRetryAllowed(call, &seen.retryAllowed)) ||
        FAILED(HCHttpCallRequestGetTimeout(call, &seen.timeout)) ||
        FAILED(HCHttpCallRequestGetTimeoutWindow(call, &seen.timeoutWindow)) ||
        FAILED(HCHttpCallRequestGetRetryDelay(call, &seen.retryDelay)) ||
        FAILED(HCHttpCallRequestGetRetryCacheId(call, &seen.retryCacheId)) ||
        FAILED(HCHttpCallRequestGetMaxReceiveBufferSize(call, &seen.maxReceiveBufferSize)))
    {
        seen.hr = E_FAIL;
    }
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
    // There is no public getter; in-box providers read the field directly.
    seen.sslValidation = call->sslValidation;
#endif
    server.observed.push_back(seen);

    HCHttpCallResponseSetStatusCode(call, 200);
    XAsyncComplete(asyncBlock, S_OK, 0);
}

struct GlobalValues
{
    bool retryAllowed;
    uint32_t timeout;
    uint32_t timeoutWindow;
    uint32_t retryDelay;
};

GlobalValues ReadGlobals()
{
    GlobalValues values{};
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryAllowed(nullptr, &values.retryAllowed));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeout(nullptr, &values.timeout));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeoutWindow(nullptr, &values.timeoutWindow));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryDelay(nullptr, &values.retryDelay));
    return values;
}

GlobalValues ReadCall(HCCallHandle call)
{
    GlobalValues values{};
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryAllowed(call, &values.retryAllowed));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeout(call, &values.timeout));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeoutWindow(call, &values.timeoutWindow));
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryDelay(call, &values.retryDelay));
    return values;
}

void VerifyValues(const GlobalValues& expected, const GlobalValues& actual)
{
    VERIFY_ARE_EQUAL(expected.retryAllowed, actual.retryAllowed);
    VERIFY_ARE_EQUAL(expected.timeout, actual.timeout);
    VERIFY_ARE_EQUAL(expected.timeoutWindow, actual.timeoutWindow);
    VERIFY_ARE_EQUAL(expected.retryDelay, actual.retryDelay);
}

const GlobalValues c_defaults{ c_defaultRetryAllowed, c_defaultTimeoutSeconds, c_defaultTimeoutWindowSeconds, c_defaultRetryDelaySeconds };

} // namespace

DEFINE_TEST_CLASS(SettingsTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(SettingsTests);

    DEFINE_TEST_CASE(TestRequestSettingDefaults)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestSettingDefaults);

        SettingsServer server;
        SettingsProviderScope scope{ SettingsPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        VerifyValues(c_defaults, ReadGlobals());

        SettingsScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VerifyValues(c_defaults, ReadCall(call.handle));

        uint32_t retryCacheId{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryCacheId(call.handle, &retryCacheId));
        VERIFY_ARE_EQUAL(0u, retryCacheId);
        size_t maxReceiveBufferSize{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetMaxReceiveBufferSize(call.handle, &maxReceiveBufferSize));
        VERIFY_ARE_EQUAL(0u, maxReceiveBufferSize); // 0 = provider default

        // The provider sees the same defaults.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(1u, server.observed.size());
        const ObservedSettings& seen = server.observed[0];
        VERIFY_SUCCEEDED(seen.hr);
        VERIFY_ARE_EQUAL(c_defaultRetryAllowed, seen.retryAllowed);
        VERIFY_ARE_EQUAL(c_defaultTimeoutSeconds, seen.timeout);
        VERIFY_ARE_EQUAL(c_defaultTimeoutWindowSeconds, seen.timeoutWindow);
        VERIFY_ARE_EQUAL(c_defaultRetryDelaySeconds, seen.retryDelay);
        VERIFY_ARE_EQUAL(0u, seen.retryCacheId);
        VERIFY_ARE_EQUAL(0u, seen.maxReceiveBufferSize);

        // Some settings exist only per call: there is no global (call == nullptr) form even though the
        // header documents "pass nullptr to set the default" for the retry cache id.
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetMaxReceiveBufferSize(nullptr, 1024));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetMaxReceiveBufferSize(nullptr, &maxReceiveBufferSize));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetRetryCacheId(nullptr, 1001));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetRetryCacheId(nullptr, &retryCacheId));

        // Getters reject null out parameters for both forms.
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetTimeout(nullptr, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetTimeout(call.handle, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetTimeoutWindow(nullptr, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetRetryDelay(nullptr, nullptr));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetRetryAllowed(nullptr, nullptr));
    }

    DEFINE_TEST_CASE(TestGlobalSettingsApplyToNewCallsOnly)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobalSettingsApplyToNewCallsOnly);

        SettingsServer server;
        SettingsProviderScope scope{ SettingsPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());
        GlobalSettingsRestorer restorer;
        VERIFY_IS_TRUE(restorer.Captured());

        SettingsScopedCall before;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&before.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(before.handle, "GET", "https://example.test/before"));

        const GlobalValues changed{ false, 45, 90, 3 };
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, changed.retryAllowed));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, changed.timeout));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(nullptr, changed.timeoutWindow));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(nullptr, changed.retryDelay));
        VerifyValues(changed, ReadGlobals());

        // Calls copy the globals when they are created: existing calls keep the old values.
        VerifyValues(c_defaults, ReadCall(before.handle));

        SettingsScopedCall after;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&after.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(after.handle, "GET", "https://example.test/after"));
        VerifyValues(changed, ReadCall(after.handle));

        // A per-call override wins for that call and does not touch the globals.
        SettingsScopedCall overridden;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&overridden.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(overridden.handle, "GET", "https://example.test/override"));
        const GlobalValues perCall{ true, 7, 8, 0 };
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(overridden.handle, perCall.retryAllowed));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(overridden.handle, perCall.timeout));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(overridden.handle, perCall.timeoutWindow));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(overridden.handle, perCall.retryDelay)); // no minimum is enforced
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(overridden.handle, 1234));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetMaxReceiveBufferSize(overridden.handle, 32 * 1024));
        VerifyValues(perCall, ReadCall(overridden.handle));
        VerifyValues(changed, ReadGlobals());

        // Providers read the per-call values.
        VERIFY_SUCCEEDED(SettingsPerformAndWait(before.handle));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(after.handle));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(overridden.handle));
        VERIFY_ARE_EQUAL(3u, server.observed.size());

        const ObservedSettings& seenBefore = server.observed[0];
        VERIFY_ARE_EQUAL(c_defaultRetryAllowed, seenBefore.retryAllowed);
        VERIFY_ARE_EQUAL(c_defaultTimeoutSeconds, seenBefore.timeout);
        VERIFY_ARE_EQUAL(c_defaultTimeoutWindowSeconds, seenBefore.timeoutWindow);
        VERIFY_ARE_EQUAL(c_defaultRetryDelaySeconds, seenBefore.retryDelay);

        const ObservedSettings& seenAfter = server.observed[1];
        VERIFY_ARE_EQUAL(changed.retryAllowed, seenAfter.retryAllowed);
        VERIFY_ARE_EQUAL(changed.timeout, seenAfter.timeout);
        VERIFY_ARE_EQUAL(changed.timeoutWindow, seenAfter.timeoutWindow);
        VERIFY_ARE_EQUAL(changed.retryDelay, seenAfter.retryDelay);

        const ObservedSettings& seenOverride = server.observed[2];
        VERIFY_ARE_EQUAL(perCall.retryAllowed, seenOverride.retryAllowed);
        VERIFY_ARE_EQUAL(perCall.timeout, seenOverride.timeout);
        VERIFY_ARE_EQUAL(perCall.timeoutWindow, seenOverride.timeoutWindow);
        VERIFY_ARE_EQUAL(perCall.retryDelay, seenOverride.retryDelay);
        VERIFY_ARE_EQUAL(1234u, seenOverride.retryCacheId);
        VERIFY_ARE_EQUAL(32u * 1024u, seenOverride.maxReceiveBufferSize);

        // Putting the defaults back makes new calls default again.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, c_defaultRetryAllowed));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, c_defaultTimeoutSeconds));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(nullptr, c_defaultTimeoutWindowSeconds));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(nullptr, c_defaultRetryDelaySeconds));
        SettingsScopedCall restored;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&restored.handle));
        VerifyValues(c_defaults, ReadCall(restored.handle));
    }

    DEFINE_TEST_CASE(TestGlobalSettingsRequireInitAndResetOnCleanup)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobalSettingsRequireInitAndResetOnCleanup);

        SettingsProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.InitHr());

        {
            GlobalSettingsRestorer restorer;
            VERIFY_IS_TRUE(restorer.Captured());
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, 99));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(nullptr, 98));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(nullptr, 97));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, false));
        }
        VerifyValues(c_defaults, ReadGlobals());

        // Changed globals are not carried across HCCleanup/HCInitialize.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, 99));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, false));
        HCCleanup();

        // Without an initialized library the global forms fail; per-call forms reject the null call.
        bool retryAllowed{ false };
        uint32_t value{ 0 };
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeout(nullptr, 5));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeout(nullptr, &value));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeoutWindow(nullptr, 5));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeoutWindow(nullptr, &value));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryDelay(nullptr, 5));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryDelay(nullptr, &value));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryAllowed(nullptr, true));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryAllowed(nullptr, &retryAllowed));

        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        VerifyValues(c_defaults, ReadGlobals());
    }

    DEFINE_TEST_CASE(TestPerCallSettingsRejectedAfterPerform)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestPerCallSettingsRejectedAfterPerform);

        SettingsServer server;
        SettingsProviderScope scope{ SettingsPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());
        GlobalSettingsRestorer restorer;

        SettingsScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(call.handle, 11));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetMaxReceiveBufferSize(call.handle, 4096));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(call.handle));

        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetTimeout(call.handle, 12));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetTimeoutWindow(call.handle, 12));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRetryDelay(call.handle, 12));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRetryAllowed(call.handle, false));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetRetryCacheId(call.handle, 12));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetMaxReceiveBufferSize(call.handle, 12));
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetHeader(call.handle, "X-Late", "1", true));
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetSSLValidation(call.handle, false));
        VERIFY_IS_TRUE(call.handle->sslValidation);
#endif

        uint32_t timeout{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeout(call.handle, &timeout));
        VERIFY_ARE_EQUAL(11u, timeout);
        size_t maxReceiveBufferSize{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetMaxReceiveBufferSize(call.handle, &maxReceiveBufferSize));
        VERIFY_ARE_EQUAL(4096u, maxReceiveBufferSize);
        uint32_t numHeaders{ 99 };
        VERIFY_SUCCEEDED(HCHttpCallRequestGetNumHeaders(call.handle, &numHeaders));
        VERIFY_ARE_EQUAL(0u, numHeaders);

        // The global forms are unaffected by any individual call having been performed.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, 12));
        VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeout(nullptr, &timeout));
        VERIFY_ARE_EQUAL(12u, timeout);
    }

#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
    DEFINE_TEST_CASE(TestSSLValidationSetting)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSSLValidationSetting);

        SettingsServer server;
        SettingsProviderScope scope{ SettingsPerform, &server };
        VERIFY_SUCCEEDED(scope.InitHr());

        // Default is to validate. There is no public getter, so read the field providers use.
        SettingsScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_IS_TRUE(call.handle->sslValidation);

        VERIFY_SUCCEEDED(HCHttpCallRequestSetSSLValidation(call.handle, false));
        VERIFY_IS_FALSE(call.handle->sslValidation);
        VERIFY_SUCCEEDED(HCHttpCallRequestSetSSLValidation(call.handle, true));
        VERIFY_IS_TRUE(call.handle->sslValidation);
        VERIFY_SUCCEEDED(HCHttpCallRequestSetSSLValidation(call.handle, false));

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetSSLValidation(nullptr, false));

        // It's a per-call setting: other calls still validate.
        SettingsScopedCall other;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&other.handle));
        VERIFY_IS_TRUE(other.handle->sslValidation);

        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://self-signed.example.test/"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(other.handle, "GET", "https://example.test/"));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(call.handle));
        VERIFY_SUCCEEDED(SettingsPerformAndWait(other.handle));
        VERIFY_ARE_EQUAL(2u, server.observed.size());
        VERIFY_IS_FALSE(server.observed[0].sslValidation);
        VERIFY_IS_TRUE(server.observed[1].sslValidation);

        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetSSLValidation(call.handle, true));
        VERIFY_IS_FALSE(call.handle->sslValidation);
    }
#endif

#if HC_PLATFORM == HC_PLATFORM_GDK || defined(HC_WINHTTP_WIN32_NOXASYNC)
    // Only compiled for GDK and the no-XAsync WinHTTP flavour; the Win32 unit-test build does not declare it.
    DEFINE_TEST_CASE(TestDisableAssertsForSSLValidationInDevSandboxes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestDisableAssertsForSSLValidationInDevSandboxes);

        HCCleanup();
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCHttpDisableAssertsForSSLValidationInDevSandboxes(HCConfigSetting::SSLValidationEnforcedInRetailSandbox));

        SettingsProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.InitHr());
        VERIFY_SUCCEEDED(HCHttpDisableAssertsForSSLValidationInDevSandboxes(HCConfigSetting::SSLValidationEnforcedInRetailSandbox));
    }
#endif

    DEFINE_TEST_CASE(TestSetGlobalProxy)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSetGlobalProxy);

        {
            // The Generic platform's HTTP provider does not implement a global proxy.
            SettingsProviderScope scope{ nullptr, nullptr };
            VERIFY_SUCCEEDED(scope.InitHr());

            VERIFY_ARE_EQUAL(E_NOTIMPL, HCSetGlobalProxy("127.0.0.1:8888"));
            VERIFY_ARE_EQUAL(E_NOTIMPL, HCSetGlobalProxy(""));
            // Null is documented as "reset to default" and must not be dereferenced.
            VERIFY_ARE_EQUAL(E_NOTIMPL, HCSetGlobalProxy(nullptr));

            // Requires an initialized library.
            HCCleanup();
            VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCSetGlobalProxy("127.0.0.1:8888"));
            VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCSetGlobalProxy(nullptr));
        }

        {
            // With a client perform callback installed the request goes to that (external) provider,
            // which does not implement a global proxy either; calls keep working.
            SettingsServer server;
            SettingsProviderScope scope{ SettingsPerform, &server };
            VERIFY_SUCCEEDED(scope.InitHr());

            VERIFY_ARE_EQUAL(E_NOTIMPL, HCSetGlobalProxy("127.0.0.1:8888"));
            VERIFY_ARE_EQUAL(E_NOTIMPL, HCSetGlobalProxy(nullptr));

            SettingsScopedCall call;
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
            VERIFY_SUCCEEDED(SettingsPerformAndWait(call.handle));
            VERIFY_ARE_EQUAL(1u, server.observed.size());
        }
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
