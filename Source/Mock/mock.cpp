// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mock.h"
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
        if (originalCall->url == mockCall->url)
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

long GetIndexOfMock(const http_internal_vector<HC_CALL*>& mocks, HC_CALL* lastMatchingMock)
{
    if (lastMatchingMock == nullptr)
    {
        return -1;
    }

    for (long i = 0; i < static_cast<long>(mocks.size()); i++)
    {
        if (mocks[i] == lastMatchingMock)
        {
            return i;
        }
    }

    return -1;
}

HC_CALL* GetMatchingMock(
    _In_ HC_CALL* originalCall
    )
{
    auto httpSingleton = get_http_singleton(false);
    if (nullptr == httpSingleton)
        return nullptr;

    http_internal_vector<HC_CALL*> mocks;
    HC_CALL* lastMatchingMock = nullptr;
    HC_CALL* matchingMock = nullptr;

    {
        std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);
        mocks = httpSingleton->m_mocks;
        lastMatchingMock = httpSingleton->m_lastMatchingMock;
    }

    // ignore last matching call if it doesn't match the current call
    if (lastMatchingMock != nullptr && !DoesMockCallMatch(lastMatchingMock, originalCall))
    {
        lastMatchingMock = nullptr;
    }

    auto lastMockIndex = GetIndexOfMock(mocks, lastMatchingMock);
    if (lastMockIndex == -1)
    {
        // if there was no last matching call, then look through all mocks for first match
        for (auto& mockCall : mocks)
        {
            if (DoesMockCallMatch(mockCall, originalCall))
            {
                matchingMock = mockCall;
                break;
            }
        }
    }
    else
    {
        // if there was last matching call, looking through the rest of the mocks to see if there's more that match
        for (auto j = lastMockIndex + 1; j < static_cast<long>(mocks.size()); j++)
        {
            if (DoesMockCallMatch(mocks[j], originalCall))
            {
                matchingMock = mocks[j];
                break;
            }
        }

        // if last after matches, then just keep returning last match
        if (matchingMock == nullptr)
        {
            matchingMock = lastMatchingMock;
        }
    }

    {
        std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);
        httpSingleton->m_lastMatchingMock = matchingMock;
    }

    return matchingMock;
}

bool Mock_Internal_HCHttpCallPerform(
    _In_ hc_call_handle_t originalCallHandle
    )
{
    HC_CALL* originalCall = static_cast<HC_CALL*>(originalCallHandle);
    HC_CALL* matchingMock = GetMatchingMock(originalCall);
    if (matchingMock == nullptr)
    {
        return false;
    }

    size_t byteBuf;
    HCHttpCallResponseGetResponseBodyBytesSize(matchingMock, &byteBuf);
    http_memory_buffer buffer(byteBuf);
    HCHttpCallResponseGetResponseBodyBytes(matchingMock, byteBuf, static_cast<uint8_t*>(buffer.get()), nullptr);
    HCHttpCallResponseSetResponseBodyBytes(originalCall, static_cast<uint8_t*>(buffer.get()), byteBuf);

    uint32_t code;
    HCHttpCallResponseGetStatusCode(matchingMock, &code);
    HCHttpCallResponseSetStatusCode(originalCall, code);

    HRESULT genCode;
    HCHttpCallResponseGetNetworkErrorCode(matchingMock, &genCode, &code);
    HCHttpCallResponseSetNetworkErrorCode(originalCall, genCode, code);

    uint32_t numheaders;
    HCHttpCallResponseGetNumHeaders(matchingMock, &numheaders);

    UTF8CSTR str1;
    UTF8CSTR str2;
    for (uint32_t i = 0; i < numheaders; i++)
    {
        HCHttpCallResponseGetHeaderAtIndex(matchingMock, i, &str1, &str2);
        HCHttpCallResponseSetHeader(originalCall, str1, str2);
    }
    
    return true;
}
