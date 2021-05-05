// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include "android_http_request.h"
#include "android_platform_context.h"

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted(JNIEnv* env, jobject instance, jlong call, jobject response)
{
    HCCallHandle sourceCall = reinterpret_cast<HCCallHandle>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    if (response == nullptr) 
    {
        HCHttpCallResponseSetNetworkErrorCode(sourceCall, E_FAIL, 0);
        XAsyncComplete(sourceRequest->GetAsyncBlock(), E_FAIL, 0);
    }
    else 
    {
        HRESULT result = sourceRequest->ProcessResponse(sourceCall, response);
        XAsyncComplete(sourceRequest->GetAsyncBlock(), result, 0);
    }
}

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed(JNIEnv* env, jobject instance, jlong call, jstring errorMessage, jboolean isNoNetwork)
{
    HCCallHandle sourceCall = reinterpret_cast<HCCallHandle>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    HCHttpCallResponseSetNetworkErrorCode(sourceCall, isNoNetwork ? E_HC_NO_NETWORK : E_FAIL, 0);

    const char* nativeErrorString = env->GetStringUTFChars(errorMessage, nullptr);
    HCHttpCallResponseSetPlatformNetworkErrorMessage(sourceCall, nativeErrorString);
    env->ReleaseStringUTFChars(errorMessage, nativeErrorString);

    XAsyncComplete(sourceRequest->GetAsyncBlock(), S_OK, 0);
}

jint ThrowIOException(JNIEnv* env, char const* message) {
    if (jclass exClass = env->FindClass("java/io/IOException")) {
        return env->ThrowNew(exClass, message);
    }
    return -1;
}

JNIEXPORT jint JNICALL Java_com_xbox_httpclient_HttpClientRequestBody_00024NativeInputStream_nativeRead(JNIEnv* env, jobject /* instance */, jlong callHandle, jlong srcOffset, jbyteArray dst, jlong dstOffset, jlong bytesAvailable)
{
    // convert call handle
    HCCallHandle call = reinterpret_cast<HCCallHandle>(callHandle);
    if (call == nullptr)
    {
        return ThrowIOException(env, "Invalid call handle");
    }

    // get read function
    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t bodySize = 0;
    void* context = nullptr;
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(call, &readFunction, &bodySize, &context);

    if (FAILED(hr) || readFunction == nullptr)
    {
        return ThrowIOException(env, "Failed to get read function");
    }

    // perform read
    size_t bytesWritten = 0;
    {
        using ByteArray = std::unique_ptr<void, std::function<void(void*)>>;
        ByteArray destination(env->GetPrimitiveArrayCritical(dst, 0), [env, dst](void* carray) {
            if (carray)
            {
                // exit critical section when this leaves scope
                env->ReleasePrimitiveArrayCritical(dst, carray, 0);
            }
        });

        if (destination == nullptr)
        {
            return ThrowIOException(env, "Buffer was null");
        }

        try
        {
            hr = readFunction(call, srcOffset, bytesAvailable, context, static_cast<uint8_t*>(destination.get()) + dstOffset, &bytesWritten);
            if (FAILED(hr))
            {
                destination.reset();
                return ThrowIOException(env, "Read function failed");
            }
        }
        catch (...)
        {
            destination.reset();
            return ThrowIOException(env, "Read function threw an exception");
        }
    }

    // make sure cast is safe
    if (bytesWritten > std::numeric_limits<jint>::max())
    {
        return ThrowIOException(env, "Unsafe cast");
    }

    return static_cast<jint>(bytesWritten);
}

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientResponse_00024NativeOutputStream_nativeWrite(JNIEnv* env, jobject /* instance */, jlong callHandle, jbyteArray src, jint sourceOffset, jint sourceLength)
{
    // convert handle
    HCCallHandle call = reinterpret_cast<HCCallHandle>(callHandle);
    if (call == nullptr)
    {
        ThrowIOException(env, "Invalid call handle");
        return;
    }

    // get write function
    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HRESULT hr = HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFunction, &context);
    if (FAILED(hr) || writeFunction == nullptr)
    {
        ThrowIOException(env, "Failed to get write function");
        return;
    }

    // perform write
    {
        using ByteArray = std::unique_ptr<void, std::function<void(void*)>>;
        ByteArray source(env->GetPrimitiveArrayCritical(src, 0), [env, src](void* carray) {
            if (carray)
            {
                // exit critical section when this leaves scope
                env->ReleasePrimitiveArrayCritical(src, carray, 0);
            }
        });

        try
        {
            hr = writeFunction(call, ((const uint8_t*)source.get()) + sourceOffset, sourceLength, writeFunction);
            if (FAILED(hr))
            {
                source.reset();
                ThrowIOException(env, "Write function failed");
                return;
            }
        }
        catch (...)
        {
            source.reset();
            ThrowIOException(env, "Write function threw an exception");
            return;
        }
    }
}

}

void Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    auto httpSingleton = xbox::httpclient::get_http_singleton();
    if (httpSingleton == nullptr)
    {
        HCHttpCallResponseSetNetworkErrorCode(call, E_HC_NOT_INITIALISED, 0);
        XAsyncComplete(asyncBlock, E_HC_NOT_INITIALISED, 0);
        return;
    }

    std::unique_ptr<HttpRequest> httpRequest{
        new HttpRequest(
            asyncBlock,
            env->GetJavaVm(),
            env->GetHttpRequestClass(),
            env->GetHttpResponseClass()
        )
    };

    HRESULT result = httpRequest->Initialize();

    if (!SUCCEEDED(result))
    {
        HCHttpCallResponseSetNetworkErrorCode(call, result, 0);
        XAsyncComplete(asyncBlock, result, 0);
        return;
    }

    const char* requestUrl = nullptr;
    const char* requestMethod = nullptr;

    HCHttpCallRequestGetUrl(call, &requestMethod, &requestUrl);
    httpRequest->SetUrl(requestUrl);

    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);

    for (uint32_t i = 0; i < numHeaders; i++) 
    {
        const char* headerName = nullptr;
        const char* headerValue = nullptr;

        HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        httpRequest->AddHeader(headerName, headerValue);
    }

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t requestBodySize = 0;
    void* context = nullptr;
    HCHttpCallRequestGetRequestBodyReadFunction(call, &readFunction, &requestBodySize, &context);

    const char* contentType = nullptr;
    if (requestBodySize > 0)
    {
        HCHttpCallRequestGetHeader(call, "Content-Type", &contentType);
    }

    httpRequest->SetMethodAndBody(call, requestMethod, contentType, requestBodySize);

    HCHttpCallSetContext(call, httpRequest.get());
    result = httpRequest->ExecuteAsync(call);

    if (SUCCEEDED(result))
    {
        httpRequest.release();
    }
    else
    { 
        XAsyncComplete(asyncBlock, E_FAIL, 0);
    }
}
