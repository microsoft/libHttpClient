#pragma once

#include <jni.h>

// These JNI functions are being exposed by this header file so that they
// can be referenced in another .cpp file. This is so the compiler is forced
// to compile these functions and prevent them from being optimized out.

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_NetworkObserver_Log(
    JNIEnv* env,
    jclass clazz,
    jstring message
);

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted(
    JNIEnv * /* env */,
    jobject /* instance */,
    jlong call,
    jobject response
);

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed(
        JNIEnv* env,
        jobject /* instance */,
        jlong call,
        jstring errorMessage,
        jstring stackTrace,
        jstring networkDetails,
        jboolean isNoNetwork
);

JNIEXPORT jint JNICALL Java_com_xbox_httpclient_HttpClientRequestBody_00024NativeInputStream_nativeRead(
        JNIEnv* env,
        jobject /* instance */,
        jlong callHandle,
        jlong srcOffset,
        jbyteArray dst,
        jlong dstOffset,
        jlong bytesAvailable
);

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientResponse_00024NativeOutputStream_nativeWrite(
        JNIEnv* env,
        jobject /* instance */,
        jlong callHandle,
        jbyteArray src,
        jint sourceOffset,
        jint sourceLength
);

}

NAMESPACE_XBOX_HTTP_CLIENT_END
