#pragma once

#include "jni.h"
#include "../httpcall.h"

class AndroidPlatformContext : public IHCPlatformContext
{
public:
    AndroidPlatformContext(JavaVM* javaVm, jobject applicationContext, jclass requestClass, jclass responseClass);
    virtual ~AndroidPlatformContext();

    JavaVM* GetJavaVm() { return m_javaVm; }
    jobject GetApplicationContext() { return m_applicationContext; }
    jclass GetHttpRequestClass() { return m_httpRequestClass; }
    jclass GetHttpResponseClass() { return m_httpResponseClass; }

private:
    JavaVM * m_javaVm;
    jobject m_applicationContext;
    jclass m_httpRequestClass;
    jclass m_httpResponseClass;
};
