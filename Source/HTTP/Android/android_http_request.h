#pragma once

#include "jni.h"

class HttpRequest {
public:
    HttpRequest();
    virtual ~HttpRequest();

    // Request Functions
    void SetUrl(const char* url);
    void SetMethodAndBody(const char* method, const char* contentType, const uint8_t* body, uint32_t bodySize);
    void AddHeader(const char* headerName, const char* headerValue);
    void ExecuteRequest();

    // Response Functions
    uint32_t GetResponseCode();
    uint32_t GetResponseHeaderCount();
    std::string GetHeaderNameAtIndex(uint32_t index);
    std::string GetHeaderValueAtIndex(uint32_t index);
    void ProcessResponseBody(hc_call_handle_t call);

    static HRESULT InitializeJavaEnvironment(JavaVM* javaVM);
    static void CleanupJavaEnvironment();
private:
    JNIEnv * m_jniEnv;
    jobject m_httpRequestInstance;
    jobject m_httpResponseInstance;

    static JavaVM* s_javaVM;
    static jclass s_httpRequestClass;
    static jclass s_httpResponseClass;
};
