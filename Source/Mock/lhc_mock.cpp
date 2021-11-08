// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "lhc_mock.h"
#include "../HTTP/httpcall.h"

using namespace xbox::httpclient;

bool DoesMockCallMatch(_In_ const HC_CALL* mockCall, _In_ const HC_CALL* originalCall)
{
    if (mockCall->url.empty())
    {
        return true;
    }
    else
    {
        if (originalCall->url.substr(0, mockCall->url.size()) == mockCall->url)
        {
            if (mockCall->requestBodyBytes.empty())
            {
                return true;
            }
            else
            {
                if (originalCall->requestBodyBytes == mockCall->requestBodyBytes)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool Mock_Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle originalCall
    )
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> guard(httpSingleton->m_mocksLock);

    if (httpSingleton->m_mocks.size() == 0)
    {
        return false;
    }

    auto& mocks{ httpSingleton->m_mocks };
    HC_MOCK_CALL* mock{ nullptr };

    // Use the most recently added mock that matches (similar to a stack).
    for (auto iter = mocks.rbegin(); iter != mocks.rend(); ++iter)
    {
        if (DoesMockCallMatch(*iter, originalCall))
        {
            mock = *iter;
            break;
        }
    }

    if (!mock)
    {
        return false;
    }

    if (mock->matchedCallback)
    {
        http_internal_vector<uint8_t> requestBodyBytes;
        HRESULT res = Mock_Internal_ReadRequestBodyIntoMemory(originalCall, &requestBodyBytes);
        if (FAILED(res))
        {
            return false;
        }

        mock->matchedCallback(
            mock,
            originalCall->method.data(),
            originalCall->url.data(),
            requestBodyBytes.data(),
            static_cast<uint32_t>(requestBodyBytes.size()),
            mock->matchCallbackContext
        );
    }

    // Read response from mock
    size_t byteBuf;
    HCHttpCallResponseGetResponseBodyBytesSize(mock, &byteBuf);
    if (byteBuf > 0)
    {
        http_memory_buffer buffer(byteBuf);
        HCHttpCallResponseGetResponseBodyBytes(mock, byteBuf, static_cast<uint8_t*>(buffer.get()), nullptr);

        // Write response to original call
        HCHttpCallResponseBodyWriteFunction writeFunction;
        void* context = nullptr;
        HCHttpCallResponseGetResponseBodyWriteFunction(originalCall, &writeFunction, &context);
        writeFunction(originalCall, static_cast<uint8_t*>(buffer.get()), byteBuf, context);
    }

    uint32_t code;
    HCHttpCallResponseGetStatusCode(mock, &code);
    HCHttpCallResponseSetStatusCode(originalCall, code);

    HRESULT genCode;
    HCHttpCallResponseGetNetworkErrorCode(mock, &genCode, &code);
    HCHttpCallResponseSetNetworkErrorCode(originalCall, genCode, code);

    uint32_t numheaders;
    HCHttpCallResponseGetNumHeaders(mock, &numheaders);

    const char* str1 = nullptr;
    const char* str2 = nullptr;
    for (uint32_t i = 0; i < numheaders; i++)
    {
        HCHttpCallResponseGetHeaderAtIndex(mock, i, &str1, &str2);
        HCHttpCallResponseSetHeader(originalCall, str1, str2);
    }

    return true;
}

HRESULT Mock_Internal_ReadRequestBodyIntoMemory(
    _In_ HCCallHandle originalCall,
    _Out_ http_internal_vector<uint8_t>* bodyBytes
    )
{
    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t bodySize = 0;
    void* context = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyReadFunction(originalCall, &readFunction, &bodySize, &context));

    http_internal_vector<uint8_t> tempBodyBytes(bodySize);

    size_t offset = 0;
    while (offset < bodySize)
    {
        size_t bytesWritten = 0;
        RETURN_IF_FAILED(readFunction(
            originalCall,
            offset,
            bodySize - offset,
            context,
            tempBodyBytes.data() + offset,
            &bytesWritten
        ));

        offset += bytesWritten;
    }

    *bodyBytes = std::move(tempBodyBytes);
    return S_OK;
}
