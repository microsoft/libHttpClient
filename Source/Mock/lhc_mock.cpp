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

    // Use the first mock that matches this call
    for (auto iter = mocks.begin(); iter != mocks.end(); ++iter)
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
        mock->matchedCallback(
            mock,
            originalCall->method.data(),
            originalCall->url.data(),
            originalCall->requestBodyBytes.data(),
            static_cast<uint32_t>(originalCall->requestBodyBytes.size()),
            mock->matchCallbackContext
        );
    }

    size_t byteBuf;
    HCHttpCallResponseGetResponseBodyBytesSize(mock, &byteBuf);
    http_memory_buffer buffer(byteBuf);
    HCHttpCallResponseGetResponseBodyBytes(mock, byteBuf, static_cast<uint8_t*>(buffer.get()), nullptr);
    HCHttpCallResponseSetResponseBodyBytes(originalCall, static_cast<uint8_t*>(buffer.get()), byteBuf);

    uint32_t code;
    HCHttpCallResponseGetStatusCode(mock, &code);
    HCHttpCallResponseSetStatusCode(originalCall, code);

    HRESULT genCode;
    HCHttpCallResponseGetNetworkErrorCode(mock, &genCode, &code);
    HCHttpCallResponseSetNetworkErrorCode(originalCall, genCode, code);

    uint32_t numheaders;
    HCHttpCallResponseGetNumHeaders(mock, &numheaders);

    const char* str1;
    const char* str2;
    for (uint32_t i = 0; i < numheaders; i++)
    {
        HCHttpCallResponseGetHeaderAtIndex(mock, i, &str1, &str2);
        HCHttpCallResponseSetHeader(originalCall, str1, str2);
    }

    // If this is not the only mock that matches, remove it from the list of mocks so that multiple can be used in sequence
    auto countMatching = std::count_if(mocks.rbegin(), mocks.rend(), [originalCall](auto m) 
        {
            return DoesMockCallMatch(m, originalCall); 
        }
    );

    if (countMatching > 1)
    {
        HCMockRemoveMock(mock);
    }

    return true;
}
