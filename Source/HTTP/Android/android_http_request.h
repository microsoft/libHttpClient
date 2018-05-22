#pragma once

#include "jni.h"

class HttpRequest {
public:
    HttpRequest(JavaVM* javaVm, jclass httpRequestClass, jclass httpResponseClass);
    virtual ~HttpRequest();

    HRESULT Initialize();

    // Request Functions
    HRESULT SetUrl(const char* url);
    HRESULT SetMethodAndBody(const char* method, const char* contentType, const uint8_t* body, uint32_t bodySize);
    HRESULT AddHeader(const char* headerName, const char* headerValue);
    HRESULT ExecuteRequest();

    // Response Functions
    uint32_t GetResponseCode();
    uint32_t GetResponseHeaderCount();
    std::string GetHeaderNameAtIndex(uint32_t index);
    std::string GetHeaderValueAtIndex(uint32_t index);
    HRESULT ProcessResponseBody(hc_call_handle_t call);
private:
    HRESULT GetJniEnv(JNIEnv**);

    jobject m_httpRequestInstance;
    jobject m_httpResponseInstance;

    JavaVM* m_javaVm;
    jclass m_httpRequestClass;
    jclass m_httpResponseClass;
};
