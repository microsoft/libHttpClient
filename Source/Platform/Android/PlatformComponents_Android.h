#pragma once

#include "jni.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class PlatformComponents_Android
{
public:
    static Result<std::shared_ptr<PlatformComponents_Android>> Initialize(HCInitArgs* args) noexcept;
    virtual ~PlatformComponents_Android();

    JavaVM* GetJavaVm() { return m_javaVm; }
    jobject GetApplicationContext() { return m_applicationContext; }
    jclass GetHttpRequestClass() { return m_httpRequestClass; }
    jclass GetHttpResponseClass() { return m_httpResponseClass; }
    jclass GetWebSocketClass() { return m_webSocketClass; }

private:
    PlatformComponents_Android(
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

NAMESPACE_XBOX_HTTP_CLIENT_END