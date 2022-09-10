#pragma once

#include "jni.h"

struct AndroidPlatformContext
{
public:
    static Result<std::shared_ptr<AndroidPlatformContext>> Initialize(HCInitArgs* args) noexcept;
    virtual ~AndroidPlatformContext();

    JavaVM* GetJavaVm() { return m_javaVm; }
    jobject GetApplicationContext() { return m_applicationContext; }
    jclass GetHttpRequestClass() { return m_httpRequestClass; }
    jclass GetHttpResponseClass() { return m_httpResponseClass; }
    jclass GetWebSocketClass() { return m_webSocketClass; }

private:
    AndroidPlatformContext(
        JavaVM* javaVm,
        jobject applicationContext,
        jclass networkObserverClass,
        jclass requestClass,
        jclass responseClass,
        jclass webSocketClass
    );

    JavaVM * m_javaVm;
    jobject m_applicationContext;
    jclass m_networkObserverClass;
    jclass m_httpRequestClass;
    jclass m_httpResponseClass;
    jclass m_webSocketClass;
};
