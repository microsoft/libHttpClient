// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "lhc_mock.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;

STDAPI HCMockCallCreate(
    _Out_ HCMockCallHandle* callHandle
    ) noexcept
{
    if (callHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    auto uniqueCall = http_allocate_unique<HC_MOCK_CALL>(++httpSingleton->m_lastId);
    HC_MOCK_CALL* call = uniqueCall.release(); // transfer ownership to raw ptr w/ ref count

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCMockCallCreate [ID %llu]", TO_ULL(call->id));

    *callHandle = call;
    return S_OK;
}

STDAPI 
HCMockAddMock(
    _In_ HCMockCallHandle call,
    _In_opt_z_ const char* method,
    _In_opt_z_ const char* url,
    _In_reads_bytes_opt_(requestBodySize) const uint8_t* requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) noexcept
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (method != nullptr && url != nullptr)
    {
        HRESULT hr = HCHttpCallRequestSetUrl(call, method, url);
        if (hr != S_OK)
        {
            return hr;
        }
    }

    if (requestBodyBytes)
    {
        HRESULT hr = HCHttpCallRequestSetRequestBodyBytes(call, requestBodyBytes, requestBodySize);
        if (hr != S_OK)
        {
            return hr;
        }
    }

    std::lock_guard<std::recursive_mutex> guard(httpSingleton->m_mocksLock);
    httpSingleton->m_mocks.push_back(static_cast<HCMockCallHandle>(HCHttpCallDuplicateHandle(call)));
    return S_OK;
}
CATCH_RETURN()

STDAPI HCMockSetMockMatchedCallback(
    _In_ HCMockCallHandle call,
    _In_ HCMockMatchedCallback callback,
    _In_opt_ void* context
)
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->matchedCallback = callback;
    call->matchCallbackContext = context;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCMockRemoveMock(
    _In_ HCMockCallHandle call
)
try
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    std::lock_guard<std::recursive_mutex> guard(httpSingleton->m_mocksLock);
    auto& mocks{ httpSingleton->m_mocks };

    for (auto iter = mocks.begin(); iter != mocks.end(); ++iter)
    {
        if (*iter == call)
        {
            mocks.erase(iter);
            HCHttpCallCloseHandle(call);
            return S_OK;
        }
    }

    return E_INVALIDARG; // Mock not found
}
CATCH_RETURN()

STDAPI 
HCMockClearMocks() noexcept
try 
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    std::lock_guard<std::recursive_mutex> guard(httpSingleton->m_mocksLock);

    for (auto& mockCall : httpSingleton->m_mocks)
    {
        HCHttpCallCloseHandle(mockCall);
    }

    httpSingleton->m_mocks.clear();
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCMockResponseSetResponseBodyBytes(
    _In_ HCMockCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ uint32_t bodySize
    ) noexcept
{
    return HCHttpCallResponseSetResponseBodyBytes(call, bodyBytes, bodySize);
}

STDAPI 
HCMockResponseSetStatusCode(
    _In_ HCMockCallHandle call,
    _In_ uint32_t statusCode
    ) noexcept
{
    return HCHttpCallResponseSetStatusCode(call, statusCode);
}

STDAPI 
HCMockResponseSetNetworkErrorCode(
    _In_ HCMockCallHandle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) noexcept
{
    return HCHttpCallResponseSetNetworkErrorCode(call, networkErrorCode, platformNetworkErrorCode);
}

STDAPI 
HCMockResponseSetHeader(
    _In_ HCMockCallHandle call,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
    ) noexcept
{
    return HCHttpCallResponseSetHeader(call, headerName, headerValue);
}

