// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "HTTP/httpcall.h"
#include "Mock/lhc_mock.h"
#include "Platform/ExternalHttpProvider.h"
#include <httpClient/mock.h>

#include <string>
#include <vector>

#pragma warning(disable:4389)

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// Status/body the stand-in "network" returns when no mock matches, so a test can tell an unmocked call
// apart from a mocked one without depending on whatever perform callback an earlier test installed.
constexpr uint32_t c_unmockedStatus = 599;
constexpr char c_unmockedBody[] = "unmocked";

void CALLBACK MockBehaviorUnmockedPerform(HCCallHandle call, XAsyncBlock* asyncBlock, void*, HCPerformEnv) noexcept
{
    HCHttpCallResponseSetStatusCode(call, c_unmockedStatus);
    HCHttpCallResponseSetResponseBodyBytes(call, reinterpret_cast<uint8_t const*>(c_unmockedBody), sizeof(c_unmockedBody) - 1);
    XAsyncComplete(asyncBlock, S_OK, 0);
}

// Installs a deterministic "unmocked" perform function, initializes the library and always tears both down,
// even when a VERIFY throws.
class MockBehaviorScope
{
public:
    MockBehaviorScope() noexcept
    {
        xbox::httpclient::ExternalHttpProvider::Get().ClearCallback();
        m_setPerformResult = HCSetHttpCallPerformFunction(&MockBehaviorUnmockedPerform, nullptr);
        m_initResult = HCInitialize(nullptr);
    }

    ~MockBehaviorScope()
    {
        if (SUCCEEDED(m_initResult))
        {
            HCMockClearMocks();
            HCCleanup();
        }
        xbox::httpclient::ExternalHttpProvider::Get().ClearCallback();
    }

    MockBehaviorScope(MockBehaviorScope const&) = delete;
    MockBehaviorScope& operator=(MockBehaviorScope const&) = delete;

    void VerifyInitialized() const
    {
        VERIFY_ARE_EQUAL(S_OK, m_setPerformResult);
        VERIFY_ARE_EQUAL(S_OK, m_initResult);
    }

private:
    HRESULT m_setPerformResult{ E_FAIL };
    HRESULT m_initResult{ E_FAIL };
};

struct PerformResult
{
    HRESULT asyncResult{ E_FAIL };
    HRESULT networkError{ E_FAIL };
    uint32_t platformError{ 0 };
    uint32_t statusCode{ 0 };
    std::string body;
};

PerformResult PerformCall(char const* method, char const* url, char const* requestBody = nullptr)
{
    HCCallHandle call = nullptr;
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, method, url));
    if (requestBody != nullptr)
    {
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, requestBody));
    }

    XAsyncBlock asyncBlock{};
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));

    PerformResult result{};
    result.asyncResult = XAsyncGetStatus(&asyncBlock, true);
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &result.networkError, &result.platformError));
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &result.statusCode));
    char const* body = nullptr;
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &body));
    result.body = body != nullptr ? body : "";
    VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
    return result;
}

// Creates a mock with the given response body and registers it. The caller's reference is released, so the
// returned handle is owned by the library and stays valid until the mock is removed/cleared.
HCMockCallHandle AddMock(
    char const* responseBody,
    char const* method,
    char const* url,
    char const* requestBody = nullptr,
    uint32_t statusCode = 200)
{
    HCMockCallHandle mock = nullptr;
    VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
    VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetStatusCode(mock, statusCode));
    VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mock, reinterpret_cast<uint8_t const*>(responseBody), static_cast<uint32_t>(strlen(responseBody))));
    VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(
        mock,
        method,
        url,
        reinterpret_cast<uint8_t const*>(requestBody),
        requestBody != nullptr ? static_cast<uint32_t>(strlen(requestBody)) : 0));
    VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));
    return mock;
}

int MockRefCount(HCMockCallHandle mock)
{
    return static_cast<HC_CALL*>(mock)->refCount.load();
}

struct MatchedCallbackRecord
{
    uint32_t invocations{ 0 };
    HCMockCallHandle matchedMock{ nullptr };
    std::string method;
    std::string url;
    std::vector<uint8_t> requestBody;
    void* context{ nullptr };

    // Optional per-call override applied to the matched mock from inside the callback.
    uint32_t overrideStatus{ 0 };
    char const* overrideBody{ nullptr };
    char const* overrideHeaderName{ nullptr };
    char const* overrideHeaderValue{ nullptr };
};

void CALLBACK RecordMockMatched(
    HCMockCallHandle matchedMock,
    char const* method,
    char const* url,
    uint8_t const* requestBodyBytes,
    uint32_t requestBodySize,
    void* context)
{
    auto record = static_cast<MatchedCallbackRecord*>(context);
    ++record->invocations;
    record->matchedMock = matchedMock;
    record->method = method != nullptr ? method : "";
    record->url = url != nullptr ? url : "";
    record->requestBody.assign(requestBodyBytes, requestBodyBytes + requestBodySize);
    record->context = context;

    if (record->overrideStatus != 0)
    {
        HCMockResponseSetStatusCode(matchedMock, record->overrideStatus);
    }
    if (record->overrideBody != nullptr)
    {
        HCMockResponseSetResponseBodyBytes(matchedMock, reinterpret_cast<uint8_t const*>(record->overrideBody), static_cast<uint32_t>(strlen(record->overrideBody)));
    }
    if (record->overrideHeaderName != nullptr)
    {
        HCMockResponseSetHeader(matchedMock, record->overrideHeaderName, record->overrideHeaderValue);
    }
}

} // namespace

DEFINE_TEST_CLASS(MockBehaviorTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(MockBehaviorTests);

    // Mock selection rule (lhc_mock.cpp): every registered mock that matches is collected in registration
    // order, and when more than one matches, successive calls with the same method+URL cycle through them
    // round-robin. A URL-specific mock gets no priority over a generic one.
    DEFINE_TEST_CASE(TestGenericAndUrlSpecificMocksCycleInRegistrationOrder)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGenericAndUrlSpecificMocksCycleInRegistrationOrder);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        AddMock("generic", nullptr, nullptr);
        AddMock("specific", "GET", "https://example.com/api");

        VERIFY_ARE_EQUAL_STR("generic", PerformCall("GET", "https://example.com/api").body.c_str());
        VERIFY_ARE_EQUAL_STR("specific", PerformCall("GET", "https://example.com/api").body.c_str());
        VERIFY_ARE_EQUAL_STR("generic", PerformCall("GET", "https://example.com/api").body.c_str());

        // Only the generic mock matches another URL; its cycle position is tracked independently.
        VERIFY_ARE_EQUAL_STR("generic", PerformCall("GET", "https://other.example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("generic", PerformCall("GET", "https://other.example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("specific", PerformCall("GET", "https://example.com/api").body.c_str());
    }

    DEFINE_TEST_CASE(TestUrlSpecificMockRegisteredFirstIsSelectedFirst)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestUrlSpecificMockRegisteredFirstIsSelectedFirst);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        AddMock("specific", "GET", "https://example.com/api");
        AddMock("generic", "", "");

        VERIFY_ARE_EQUAL_STR("specific", PerformCall("GET", "https://example.com/api").body.c_str());
        VERIFY_ARE_EQUAL_STR("generic", PerformCall("GET", "https://example.com/api").body.c_str());
        VERIFY_ARE_EQUAL_STR("specific", PerformCall("GET", "https://example.com/api").body.c_str());
    }

    DEFINE_TEST_CASE(TestUrlMockMatchesByPrefix)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestUrlMockMatchesByPrefix);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        AddMock("prefix", "GET", "https://example.com/api");

        VERIFY_ARE_EQUAL_STR("prefix", PerformCall("GET", "https://example.com/api").body.c_str());
        VERIFY_ARE_EQUAL_STR("prefix", PerformCall("GET", "https://example.com/api/v1/items?x=1").body.c_str());

        PerformResult shorter = PerformCall("GET", "https://example.com/ap");
        VERIFY_ARE_EQUAL(c_unmockedStatus, shorter.statusCode);
        VERIFY_ARE_EQUAL_STR(c_unmockedBody, shorter.body.c_str());

        PerformResult otherHost = PerformCall("GET", "https://other.com/api");
        VERIFY_ARE_EQUAL(c_unmockedStatus, otherHost.statusCode);
    }

    // Documents current matching quirks in DoesMockCallMatch (lhc_mock.cpp): the HTTP method is never compared,
    // a URL is only recorded when both method and URL are non-null, and a request body is only compared when
    // the mock also has a URL.
    DEFINE_TEST_CASE(TestMockMatchingIgnoresMethodAndBodyWithoutUrl)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMockMatchingIgnoresMethodAndBodyWithoutUrl);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle postOnly = AddMock("post-mock", "POST", "https://example.com/items");
        VERIFY_ARE_EQUAL_STR("post-mock", PerformCall("GET", "https://example.com/items").body.c_str());
        VERIFY_ARE_EQUAL_STR("post-mock", PerformCall("DELETE", "https://example.com/items").body.c_str());
        VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(postOnly));

        // URL without a method: the URL is not recorded, so the mock is generic.
        HCMockCallHandle urlWithoutMethod = AddMock("no-method", nullptr, "https://example.com/items");
        VERIFY_ARE_EQUAL_STR("no-method", PerformCall("GET", "https://unrelated.example.org/").body.c_str());
        VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(urlWithoutMethod));

        // Body without a URL: the body is ignored, so the mock is generic.
        AddMock("body-only", nullptr, nullptr, "expected-body");
        VERIFY_ARE_EQUAL_STR("body-only", PerformCall("POST", "https://example.com/items", "different-body").body.c_str());
        VERIFY_ARE_EQUAL_STR("body-only", PerformCall("GET", "https://example.com/items").body.c_str());
    }

    DEFINE_TEST_CASE(TestBodyMatchingMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestBodyMatchingMock);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        AddMock("body-match", "POST", "https://example.com/items", "{\"id\":1}");

        VERIFY_ARE_EQUAL_STR("body-match", PerformCall("POST", "https://example.com/items", "{\"id\":1}").body.c_str());

        PerformResult differentBody = PerformCall("POST", "https://example.com/items", "{\"id\":2}");
        VERIFY_ARE_EQUAL(c_unmockedStatus, differentBody.statusCode);
        VERIFY_ARE_EQUAL_STR(c_unmockedBody, differentBody.body.c_str());

        PerformResult noBody = PerformCall("POST", "https://example.com/items");
        VERIFY_ARE_EQUAL(c_unmockedStatus, noBody.statusCode);

        // A URL-only mock also matches the same call; with both registered the body mock does not win, they cycle.
        AddMock("url-only", "POST", "https://example.com/items");
        VERIFY_ARE_EQUAL_STR("body-match", PerformCall("POST", "https://example.com/items", "{\"id\":1}").body.c_str());
        VERIFY_ARE_EQUAL_STR("url-only", PerformCall("POST", "https://example.com/items", "{\"id\":1}").body.c_str());

        // When the body differs only the URL-only mock matches.
        VERIFY_ARE_EQUAL_STR("url-only", PerformCall("POST", "https://example.com/items", "{\"id\":2}").body.c_str());
        VERIFY_ARE_EQUAL_STR("url-only", PerformCall("POST", "https://example.com/items", "{\"id\":2}").body.c_str());
    }

    DEFINE_TEST_CASE(TestRemoveMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRemoveMock);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle first = AddMock("first", "GET", "https://example.com/");
        AddMock("second", "GET", "https://example.com/");

        VERIFY_ARE_EQUAL_STR("first", PerformCall("GET", "https://example.com/").body.c_str());

        VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(first));
        VERIFY_ARE_EQUAL_STR("second", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("second", PerformCall("GET", "https://example.com/").body.c_str());

        // Removing an unregistered mock fails.
        HCMockCallHandle neverAdded = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&neverAdded));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCMockRemoveMock(neverAdded));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(neverAdded));

        VERIFY_ARE_EQUAL(S_OK, HCMockClearMocks());
        PerformResult afterClear = PerformCall("GET", "https://example.com/");
        VERIFY_ARE_EQUAL(c_unmockedStatus, afterClear.statusCode);
    }

    DEFINE_TEST_CASE(TestRemoveMockResetsCycle)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRemoveMockResetsCycle);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        AddMock("a", "GET", "https://example.com/");
        AddMock("b", "GET", "https://example.com/");
        HCMockCallHandle c = AddMock("c", "GET", "https://example.com/");

        VERIFY_ARE_EQUAL_STR("a", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("b", PerformCall("GET", "https://example.com/").body.c_str());

        // Removing any mock resets all cycle positions, so the next call starts again with the first match.
        VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(c));
        VERIFY_ARE_EQUAL_STR("a", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("b", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("a", PerformCall("GET", "https://example.com/").body.c_str());
    }

    DEFINE_TEST_CASE(TestMockHandleRefCounting)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMockHandleRefCounting);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle mock = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
        VERIFY_ARE_EQUAL(1, MockRefCount(mock));

        HCMockCallHandle duplicate = HCMockCallDuplicateHandle(mock);
        VERIFY_IS_TRUE(duplicate == mock);
        VERIFY_ARE_EQUAL(2, MockRefCount(mock));
        VERIFY_IS_NULL(HCMockCallDuplicateHandle(nullptr));

        uint8_t const body[] = { 'o', 'k' };
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mock, body, sizeof(body)));
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mock, nullptr, nullptr, nullptr, 0));
        VERIFY_ARE_EQUAL(3, MockRefCount(mock)); // the mock list holds its own reference

        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(duplicate));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));
        VERIFY_ARE_EQUAL(1, MockRefCount(mock));

        // The registered mock must still be usable after the client released all of its handles.
        VERIFY_ARE_EQUAL_STR("ok", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL_STR("ok", PerformCall("GET", "https://example.com/").body.c_str());
        VERIFY_ARE_EQUAL(1, MockRefCount(mock));

        // Keep a reference across removal so the count can be observed, then release it.
        HCMockCallDuplicateHandle(mock);
        VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(mock));
        VERIFY_ARE_EQUAL(1, MockRefCount(mock));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCMockCallCloseHandle(nullptr));
    }

    DEFINE_TEST_CASE(TestMockMatchedCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMockMatchedCallback);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle mock = AddMock("original", "POST", "https://example.com/items", nullptr, 200);
        MatchedCallbackRecord record{};
        VERIFY_ARE_EQUAL(S_OK, HCMockSetMockMatchedCallback(mock, &RecordMockMatched, &record));

        PerformResult first = PerformCall("POST", "https://example.com/items/42", "payload\x01");
        VERIFY_ARE_EQUAL(1u, record.invocations);
        VERIFY_IS_TRUE(record.matchedMock == mock);
        VERIFY_IS_TRUE(record.context == &record);
        VERIFY_ARE_EQUAL_STR("POST", record.method.c_str());
        VERIFY_ARE_EQUAL_STR("https://example.com/items/42", record.url.c_str());
        VERIFY_IS_TRUE(std::string(record.requestBody.begin(), record.requestBody.end()) == "payload\x01");
        VERIFY_ARE_EQUAL(200u, first.statusCode);
        VERIFY_ARE_EQUAL_STR("original", first.body.c_str());

        // The callback runs before the mock's response is copied to the call, so it can override it per call.
        record.overrideStatus = 418;
        record.overrideBody = "overridden";
        record.overrideHeaderName = "X-Override";
        record.overrideHeaderValue = "yes";

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "POST", "https://example.com/items"));
        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&asyncBlock, true));

        VERIFY_ARE_EQUAL(2u, record.invocations);
        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(record.requestBody.size()));
        uint32_t status = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &status));
        VERIFY_ARE_EQUAL(418u, status);
        char const* body = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &body));
        VERIFY_ARE_EQUAL_STR("overridden", body);
        char const* header = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "x-override", &header));
        VERIFY_ARE_EQUAL_STR("yes", header);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));

        // A call that matches no mock does not invoke the callback.
        PerformResult unmatched = PerformCall("POST", "https://other.example.com/");
        VERIFY_ARE_EQUAL(c_unmockedStatus, unmatched.statusCode);
        VERIFY_ARE_EQUAL(2u, record.invocations);

        // Clearing the callback stops invocations.
        VERIFY_ARE_EQUAL(S_OK, HCMockSetMockMatchedCallback(mock, nullptr, nullptr));
        PerformCall("POST", "https://example.com/items");
        VERIFY_ARE_EQUAL(2u, record.invocations);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCMockSetMockMatchedCallback(nullptr, &RecordMockMatched, nullptr));
    }

    DEFINE_TEST_CASE(TestMockedResponseSurfacesOnCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMockedResponseSurfacesOnCall);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle mock = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetStatusCode(mock, 201));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetNetworkErrorCode(mock, S_OK, 0));
        uint8_t const binaryBody[] = { 0x00, 0x01, 0xFF, 0x7F, 0x00 };
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mock, binaryBody, sizeof(binaryBody)));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetHeader(mock, "Content-Type", "application/octet-stream"));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetHeader(mock, "X-Request-Id", "abc123"));
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mock, "GET", "https://example.com/blob", nullptr, 0));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "GET", "https://example.com/blob"));
        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&asyncBlock, true));

        uint32_t status = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &status));
        VERIFY_ARE_EQUAL(201u, status);

        size_t bodySize = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseBodyBytesSize(call, &bodySize));
        VERIFY_ARE_EQUAL(sizeof(binaryBody), bodySize);
        uint8_t bodyBuffer[sizeof(binaryBody)]{};
        size_t bodyUsed = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseBodyBytes(call, sizeof(bodyBuffer), bodyBuffer, &bodyUsed));
        VERIFY_ARE_EQUAL(sizeof(binaryBody), bodyUsed);
        VERIFY_IS_TRUE(memcmp(binaryBody, bodyBuffer, sizeof(binaryBody)) == 0);

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2u, numHeaders);
        char const* value = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "content-type", &value));
        VERIFY_ARE_EQUAL_STR("application/octet-stream", value);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "X-REQUEST-ID", &value));
        VERIFY_ARE_EQUAL_STR("abc123", value);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
    }

    DEFINE_TEST_CASE(TestMockedNetworkError)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMockedNetworkError);
        MockBehaviorScope scope;
        scope.VerifyInitialized();

        HCMockCallHandle mock = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetNetworkErrorCode(mock, E_HC_NO_NETWORK, 12029));
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mock, "GET", "https://offline.example.com/", nullptr, 0));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));

        PerformResult result = PerformCall("GET", "https://offline.example.com/");
        // The perform itself succeeds; the failure is reported through the network error code.
        VERIFY_ARE_EQUAL(S_OK, result.asyncResult);
        VERIFY_ARE_EQUAL(E_HC_NO_NETWORK, result.networkError);
        VERIFY_ARE_EQUAL(12029u, result.platformError);
        VERIFY_ARE_EQUAL(0u, result.statusCode);
        VERIFY_ARE_EQUAL_STR("", result.body.c_str());
    }

    DEFINE_TEST_CASE(TestMocksClearedByCleanup)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMocksClearedByCleanup);

        xbox::httpclient::ExternalHttpProvider::Get().ClearCallback();
        VERIFY_ARE_EQUAL(S_OK, HCSetHttpCallPerformFunction(&MockBehaviorUnmockedPerform, nullptr));

        // Mock APIs that need the singleton fail before initialization.
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCMockClearMocks());
        HCMockCallHandle uninitialized = nullptr;
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, HCMockCallCreate(&uninitialized));

        HCMockCallHandle mock = nullptr;
        {
            MockBehaviorScope scope;
            scope.VerifyInitialized();

            VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
            uint8_t const body[] = { 'm' };
            VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mock, body, sizeof(body)));
            VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mock, nullptr, nullptr, nullptr, 0));
            VERIFY_ARE_EQUAL(2, MockRefCount(mock));
            VERIFY_ARE_EQUAL_STR("m", PerformCall("GET", "https://example.com/").body.c_str());

            VERIFY_ARE_EQUAL(S_OK, HCMockRemoveMock(mock));
            VERIFY_ARE_EQUAL(1, MockRefCount(mock));
            VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mock, nullptr, nullptr, nullptr, 0));
            VERIFY_ARE_EQUAL(2, MockRefCount(mock));

            // Cleanup without HCMockClearMocks.
            HCCleanup();
            VERIFY_ARE_EQUAL(1, MockRefCount(mock)); // the library released its reference, ours is intact

            // Re-initialize: the mock from the previous session is gone.
            VERIFY_ARE_EQUAL(S_OK, HCSetHttpCallPerformFunction(&MockBehaviorUnmockedPerform, nullptr));
            VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
            PerformResult afterReinit = PerformCall("GET", "https://example.com/");
            VERIFY_ARE_EQUAL(c_unmockedStatus, afterReinit.statusCode);
            VERIFY_ARE_EQUAL(E_INVALIDARG, HCMockRemoveMock(mock));
        } // scope performs the final HCCleanup and clears the perform callback

        VERIFY_ARE_EQUAL(S_OK, HCMockCallCloseHandle(mock));
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
