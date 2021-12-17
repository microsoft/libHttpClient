#pragma once

#include <jni.h>

extern "C"
{

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
