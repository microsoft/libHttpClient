// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include "android_http_request.h"
#include "http_android.h"

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

/// <summary>
/// Logs the contents of the given jstring, line by line. The provided line format string should
/// contain a single "%.*s" format arg.
/// </summary>
void LogByLine(JNIEnv* env, jstring javaString, char const* intro, char const* lineFormat)
{
    auto stringLength = static_cast<uint32_t>(env->GetStringLength(javaString));
    const char* nativeString = env->GetStringUTFChars(javaString, nullptr);
    std::string_view nativeStringView{ nativeString, stringLength };

    HC_TRACE_WARNING(HTTPCLIENT, "%s", intro);

    while (!nativeStringView.empty())
    {
        size_t nextNewline = nativeStringView.find('\n');
        std::string_view line = nativeStringView.substr(0, nextNewline);

        HC_TRACE_WARNING(HTTPCLIENT, lineFormat, line.size(), line.data());

        if (nextNewline == std::string::npos)
        {
            break;
        }
        else
        {
            nativeStringView = nativeStringView.substr(nextNewline + 1);
        }
    }

    env->ReleaseStringUTFChars(javaString, nativeString);
}

}

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_NetworkObserver_Log(
    JNIEnv* env,
    jclass /*clazz*/,
    jstring message
)
{
    const char* nativeMessage = env->GetStringUTFChars(message, nullptr);
    HC_TRACE_IMPORTANT(HTTPCLIENT, "NetworkObserver: %s", nativeMessage);
    env->ReleaseStringUTFChars(message, nativeMessage);
}

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
    jstring networkDetails,
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

    // Log the stack trace and network details for debugging purposes. They may
    // be very long, so we log them by line.

    LogByLine(env, stackTrace,
        "Network request failed, stack trace:", // intro
        "  %.*s" // lineFormat
    );

    LogByLine(env, networkDetails,
        "Network request failed, all network details:", // intro
        "  %.*s" // lineFormat
    );

    XAsyncComplete(sourceRequest->GetAsyncBlock(), S_OK, 0);
}

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_ReportProgress(
        JNIEnv* env,
        jobject /* instance */,
        jlong call,
        jlong current,
        jlong total,
        jboolean isUpload
)
{
    auto sourceCall = reinterpret_cast<HCCallHandle>(call);
    size_t minimumInterval = 0;
    std::chrono::steady_clock::time_point* lastProgressReport;

    if (isUpload) {
        minimumInterval = sourceCall->uploadMinimumProgressReportInterval;
        lastProgressReport = &sourceCall->uploadLastProgressReport;
    }
    else {
        minimumInterval = sourceCall->downloadMinimumProgressReportInterval;
        lastProgressReport = &sourceCall->downloadLastProgressReport;
    }

    HCHttpCallProgressReportFunction progressReportFunction = nullptr;
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(sourceCall, isUpload, &progressReportFunction);
    if (FAILED(hr)) {
        const char* functionStr = isUpload ? "upload function" : "download function";
        std::string msg = "Java_com_xbox_httpclient_HttpClientRequest_ReportProgress: failed getting Progress Report ";
        msg.append(functionStr);
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, msg.c_str());
    }

    if (progressReportFunction != nullptr)
    {
        long minimumProgressReportIntervalInMs = static_cast<long>(minimumInterval * 1000);

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastProgressReport).count();

        if (elapsed >= minimumProgressReportIntervalInMs)
        {
            HRESULT hr = progressReportFunction(sourceCall, (int)current, (int)total);
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Java_com_xbox_httpclient_HttpClientRequest_ReportProgress: something went wrong after invoking the progress callback function.");
            }

            *lastProgressReport = now;
        }
    }
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

NAMESPACE_XBOX_HTTP_CLIENT_END
