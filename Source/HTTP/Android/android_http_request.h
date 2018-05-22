#pragma once

#include "jni.h"

class HttpRequest {
public:
    HttpRequest();
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

    static HRESULT InitializeJavaEnvironment(JavaVM* javaVM);
    static HRESULT CleanupJavaEnvironment();
private:
    HRESULT GetJniEnv(JNIEnv**);

    jobject m_httpRequestInstance;
    jobject m_httpResponseInstance;

    static JavaVM* s_javaVm;
    static jclass s_httpRequestClass;
    static jclass s_httpResponseClass;
};
