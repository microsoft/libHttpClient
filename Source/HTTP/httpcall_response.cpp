// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

STDAPI
HCHttpCallResponseGetResponseBodyWriteFunction(
    _In_ HCCallHandle call,
    _Out_ HCHttpCallResponseBodyWriteFunction* writeFunction,
    _Out_ void** context
) noexcept
try
{
    if (call == nullptr || writeFunction == nullptr || context == nullptr)
    {
        return E_INVALIDARG;
    }

    *writeFunction = call->responseBodyWriteFunction;
    *context = call->responseBodyWriteFunctionContext;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCHttpCallResponseSetResponseBodyWriteFunction(
    _In_ HCCallHandle call,
    _In_ HCHttpCallResponseBodyWriteFunction writeFunction,
    _In_opt_ void* context
    ) noexcept
try
{
    if (call == nullptr || writeFunction == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    call->responseBodyWriteFunction = writeFunction;
    call->responseBodyWriteFunctionContext = context;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetResponseString(
    _In_ HCCallHandle call,
    _Out_ const char** responseString
    ) noexcept
try
{
    if (call == nullptr || responseString == nullptr)
    {
        return E_INVALIDARG;
    }

    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (writeFunction != HC_CALL::ResponseBodyWrite)
    {
        return E_FAIL;
    }

    if (call->responseString.empty())
    {
        call->responseString = http_internal_string(reinterpret_cast<char const*>(call->responseBodyBytes.data()), call->responseBodyBytes.size());
        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseGetResponseString [ID %llu]: responseString=%.2048s", TO_ULL(call->id), call->responseString.c_str()); }
    }
    *responseString = call->responseString.c_str();
    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallResponseGetResponseBodyBytesSize(
    _In_ HCCallHandle call,
    _Out_ size_t* bufferSize
    ) noexcept
try
{
    if (call == nullptr || bufferSize == nullptr)
    {
        return E_INVALIDARG;
    }

    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (writeFunction != HC_CALL::ResponseBodyWrite)
    {
        return E_FAIL;
    }

    *bufferSize = call->responseBodyBytes.size();
    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallResponseGetResponseBodyBytes(
    _In_ HCCallHandle call,
    _In_ size_t bufferSize,
    _Out_writes_bytes_to_opt_(bufferSize, *bufferUsed) uint8_t* buffer,
    _Out_opt_ size_t* bufferUsed
    ) noexcept
try
{
    if (call == nullptr || buffer == nullptr)
    {
        return E_INVALIDARG;
    }

    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (writeFunction != HC_CALL::ResponseBodyWrite)
    {
        return E_FAIL;
    }

    if (call->responseBodyBytes.size() > bufferSize)
    {
        return E_BOUNDS;
    }

#if HC_PLATFORM_IS_MICROSOFT
    memcpy_s(buffer, bufferSize, call->responseBodyBytes.data(), call->responseBodyBytes.size());
#else
    memcpy(buffer, call->responseBodyBytes.data(), call->responseBodyBytes.size());
#endif

    if (bufferUsed != nullptr)
    {
        *bufferUsed = call->responseBodyBytes.size();
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseSetResponseBodyBytes(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ size_t bodySize
    ) noexcept
try 
{
    if (call == nullptr || bodyBytes == nullptr)
    {
        return E_INVALIDARG;
    }

    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (writeFunction != HC_CALL::ResponseBodyWrite)
    {
        return E_FAIL;
    }

    call->responseBodyBytes.assign(bodyBytes, bodyBytes + bodySize);
    call->responseString.clear();

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseBodyBytes [ID %llu]: bodySize=%zu", TO_ULL(call->id), bodySize); }
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCHttpCallResponseAppendResponseBodyBytes(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ size_t bodySize
) noexcept
try
{
    if (call == nullptr || bodyBytes == nullptr)
    {
        return E_INVALIDARG;
    }

    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (writeFunction != HC_CALL::ResponseBodyWrite)
    {
        return E_FAIL;
    }

    call->responseBodyBytes.insert(call->responseBodyBytes.end(), bodyBytes, bodyBytes + bodySize);
    call->responseString.clear();

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseAppendResponseBodyBytes [ID %llu]: bodySize=%zu (total=%llu)", TO_ULL(call->id), bodySize, call->responseBodyBytes.size()); }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetStatusCode(
    _In_ HCCallHandle call,
    _Out_ uint32_t* statusCode
    ) noexcept
try 
{
    if (call == nullptr || statusCode == nullptr)
    {
        return E_INVALIDARG;
    }

    *statusCode = call->statusCode;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseSetStatusCode(
    _In_ HCCallHandle call,
    _In_ uint32_t statusCode
    ) noexcept
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->statusCode = statusCode;
    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u", TO_ULL(call->id), statusCode); }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetNetworkErrorCode(
    _In_ HCCallHandle call,
    _Out_ HRESULT* networkErrorCode,
    _Out_ uint32_t* platformNetworkErrorCode
    ) noexcept
try 
{
    if (call == nullptr || networkErrorCode == nullptr || platformNetworkErrorCode == nullptr)
    {
        return E_INVALIDARG;
    }

    *networkErrorCode = call->networkErrorCode;
    *platformNetworkErrorCode = call->platformNetworkErrorCode;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseSetNetworkErrorCode(
    _In_ HCCallHandle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) noexcept
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->networkErrorCode = networkErrorCode;
    call->platformNetworkErrorCode = platformNetworkErrorCode;
    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X (%08X)", TO_ULL(call->id), networkErrorCode, platformNetworkErrorCode); }
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCHttpCallResponseGetPlatformNetworkErrorMessage(
    _In_ HCCallHandle call,
    _Out_ const char** platformNetworkErrorMessage
    ) noexcept
try
{
    if (call == nullptr || platformNetworkErrorMessage == nullptr)
    {
        return E_INVALIDARG;
    }

    *platformNetworkErrorMessage = call->platformNetworkErrorMessage.c_str();
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCHttpCallResponseSetPlatformNetworkErrorMessage(
    _In_ HCCallHandle call,
    _In_z_ const char* platformNetworkErrorMessage
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }
    // do a thing to/from string
    call->platformNetworkErrorMessage = platformNetworkErrorMessage;
    if (call->traceCall)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT,
            "HCHttpCallResponseSetErrorMessage [ID %llu]: errorMessage=%s",
            TO_ULL(call->id),
            platformNetworkErrorMessage);
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
    ) noexcept
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    auto it = call->responseHeaders.find(headerName);
    if (it != call->responseHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetNumHeaders(
    _In_ HCCallHandle call,
    _Out_ uint32_t* numHeaders
    ) noexcept
try 
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(call->responseHeaders.size());
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HCCallHandle call,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
    ) noexcept
try
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = call->responseHeaders.cbegin(); it != call->responseHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return S_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseSetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
) noexcept
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    return HCHttpCallResponseSetHeaderWithLength(
        call,
        headerName,
        strlen(headerName),
        headerValue,
        strlen(headerValue)
    );
}

STDAPI HCHttpCallResponseSetHeaderWithLength(
    _In_ HCCallHandle call,
    _In_reads_(nameSize) const char* headerName,
    _In_ size_t nameSize,
    _In_reads_(valueSize) const char* headerValue,
    _In_ size_t valueSize
    ) noexcept
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    http_internal_string name{ headerName, headerName + nameSize };

    auto it = call->responseHeaders.find(name);
    if (it != call->responseHeaders.end())
    {
        // Duplicated response header found. We must concatenate it with the existing headers
        http_internal_string& newHeaderValue = it->second;
        newHeaderValue.append(", ");
        newHeaderValue.append(headerValue, headerValue + valueSize);

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: Duplicated header %s=%s", TO_ULL(call->id), name.c_str(), newHeaderValue.c_str()); }
    }
    else
    {
        http_internal_string value{ headerValue, headerValue + valueSize };

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %s=%s", TO_ULL(call->id), name.c_str(), value.c_str()); }

        call->responseHeaders[name] = std::move(value);
    }

    return S_OK;
}
CATCH_RETURN()


