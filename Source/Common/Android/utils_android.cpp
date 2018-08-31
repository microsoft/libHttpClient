// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "utils_android.h"
#include "../HTTP/Android/android_platform_context.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

JNIEnv* get_jvm_env()
{
    auto httpSingleton = xbox::httpclient::get_http_singleton(true);
    AndroidPlatformContext* platformContext = reinterpret_cast<AndroidPlatformContext*>(httpSingleton->m_platformContext.get());
    JavaVM* javaVm = platformContext->GetJavaVm();

    if (javaVm == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "javaVm is null");
        throw std::runtime_error("JavaVm is null");
    }

    JNIEnv* jniEnv = nullptr;
    jint jniResult = javaVm->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (jniResult != JNI_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not initialize HTTP request object, JavaVM is not attached to a java thread. %d", jniResult);
        throw std::runtime_error("This thread is not attached to a the JavaVm");
    }

    return jniEnv;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
