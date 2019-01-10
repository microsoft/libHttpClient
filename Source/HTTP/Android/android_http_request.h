#pragma once

#include "jni.h"

class HttpRequest {
public:
    HttpRequest(XAsyncBlock* asyncBlock, JavaVM* javaVm, jobject applicationContext, jclass httpRequestClass, jclass httpResponseClass);
    virtual ~HttpRequest();

    HRESULT Initialize();
    XAsyncBlock* GetAsyncBlock() { return m_asyncBlock; }

    // Request Functions
    HRESULT SetUrl(const char* url);
    HRESULT SetMethodAndBody(const char* method, const char* contentType, const uint8_t* body, uint32_t bodySize);
    HRESULT AddHeader(const char* headerName, const char* headerValue);
    HRESULT ExecuteAsync(HCCallHandle call);

    HRESULT ProcessResponse(HCCallHandle call, jobject response);

private:
    HRESULT GetJniEnv(JNIEnv**);
    uint32_t GetResponseHeaderCount(jobject response);
    HRESULT ProcessResponseBody(HCCallHandle call, jobject response);

    jobject m_httpRequestInstance;
    XAsyncBlock* m_asyncBlock;

    JavaVM* m_javaVm;
    jobject m_applicationContext;
    jclass m_httpRequestClass;
    jclass m_httpResponseClass;
};
