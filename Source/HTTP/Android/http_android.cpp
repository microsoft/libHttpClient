// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include "android_http_request.h"
#include "android_platform_context.h"
#include "http_android.h"

// Helpers for converting jbyteArrays to be useful
namespace
{

struct ByteArrayDeleter
{
    void operator()(jbyte* ptr) const noexcept
    {
        if (ptr)
        {
            Env->ReleaseByteArrayElements(Src, ptr, copyBack ? 0 : JNI_ABORT);
        }
    }

    JNIEnv* Env;
    jbyteArray Src;
    bool copyBack;
};

using ByteArray = std::unique_ptr<jbyte, ByteArrayDeleter>;

ByteArray GetBytesFromJByteArray(JNIEnv* env, jbyteArray array, bool copyBack)
{
    return ByteArray{ env->GetByteArrayElements(array, nullptr), ByteArrayDeleter{ env, array, copyBack } };
}

}

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted(
    JNIEnv* /* env */,
    jobject /* instance */,
    jlong call,
    jobject response
)
{
    auto sourceCall = reinterpret_cast<HCCallHandle>(call);
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

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed(
    JNIEnv* env,
    jobject /* instance */,
    jlong call,
    jstring errorMessage,
    jstring stackTrace,
    jboolean isNoNetwork
)
{
    auto sourceCall = reinterpret_cast<HCCallHandle>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    HCHttpCallResponseSetNetworkErrorCode(sourceCall, isNoNetwork ? E_HC_NO_NETWORK : E_FAIL, 0);

    const char* nativeErrorString = env->GetStringUTFChars(errorMessage, nullptr);
    HCHttpCallResponseSetPlatformNetworkErrorMessage(sourceCall, nativeErrorString);
    env->ReleaseStringUTFChars(errorMessage, nativeErrorString);

    // Log the stack trace for debugging purposes. It may be very long, so we
    // break it on newlines.
    auto stringLength = static_cast<uint32_t>(env->GetStringLength(stackTrace));
    const char* nativeStackTraceString = env->GetStringUTFChars(stackTrace, nullptr);

    std::string_view stackTraceStringView{ nativeStackTraceString, stringLength };
    bool firstLine = true;
    while (!stackTraceStringView.empty())
    {
        size_t nextNewline = stackTraceStringView.find('\n');
        std::string_view line = stackTraceStringView.substr(0, nextNewline);

        char const* format = firstLine ? "Network request failed, stack trace: %.*s" : "  %.*s";
        HC_TRACE_WARNING(HTTPCLIENT, format, line.size(), line.data());

        firstLine = false;

        if (nextNewline == std::string::npos)
        {
            break;
        }
        else
        {
            stackTraceStringView = stackTraceStringView.substr(nextNewline + 1);
        }
    }
    env->ReleaseStringUTFChars(stackTrace, nativeStackTraceString);

    XAsyncComplete(sourceRequest->GetAsyncBlock(), S_OK, 0);
}

jint ThrowIOException(JNIEnv* env, char const* message) {
    if (jclass exClass = env->FindClass("java/io/IOException")) {
        return env->ThrowNew(exClass, message);
    }
    return -1;
}

JNIEXPORT jint JNICALL Java_com_xbox_httpclient_HttpClientRequestBody_00024NativeInputStream_nativeRead(
    JNIEnv* env,
    jobject /* instance */,
    jlong callHandle,
    jlong srcOffset,
    jbyteArray dst,
    jlong dstOffset,
    jlong bytesAvailable
)
{
    // convert call handle
    auto call = reinterpret_cast<HCCallHandle>(callHandle);
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

    if (srcOffset >= bodySize)
    {
        // Signal to Java-land that we are done reading
        return -1;
    }

    // perform read
    size_t bytesWritten = 0;
    {
        ByteArray destination = GetBytesFromJByteArray(env, dst, true /* copyBack */);

        if (destination == nullptr)
        {
            return ThrowIOException(env, "Buffer was null");
        }

        try
        {
            hr = readFunction(call, srcOffset, bytesAvailable, context, reinterpret_cast<uint8_t*>(destination.get()) + dstOffset, &bytesWritten);
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

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientResponse_00024NativeOutputStream_nativeWrite(
    JNIEnv* env,
    jobject /* instance */,
    jlong callHandle,
    jbyteArray src,
    jint sourceOffset,
    jint sourceLength
)
{
    // convert handle
    auto call = reinterpret_cast<HCCallHandle>(callHandle);
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
        ByteArray source = GetBytesFromJByteArray(env, src, false /* copyBack */);

        try
        {
            hr = writeFunction(call, reinterpret_cast<uint8_t*>(source.get()) + sourceOffset, sourceLength, context);
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
