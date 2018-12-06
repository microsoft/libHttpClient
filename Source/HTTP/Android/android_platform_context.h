#pragma once

#include "jni.h"
#include "../httpcall.h"

struct HC_PERFORM_ENV
{
public:
    HC_PERFORM_ENV(JavaVM* javaVm, jobject applicationContext, jclass requestClass, jclass responseClass);
    virtual ~HC_PERFORM_ENV();

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
