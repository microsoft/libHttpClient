#pragma once

#include "jni.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class HttpRequest {
public:
    HttpRequest(
        XAsyncBlock* asyncBlock,
        JavaVM* javaVm,
        jobject applicationContext,
        jclass httpRequestClass,
        jclass httpResponseClass
    );
    virtual ~HttpRequest();

    HRESULT Initialize();
    XAsyncBlock* GetAsyncBlock() { return m_asyncBlock; }

    // Request Functions
    HRESULT SetUrl(const char* url);
    HRESULT SetMethodAndBody(HCCallHandle call, const char* method, const char* contentType, uint32_t bodySize);
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

void AndroidHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept;

NAMESPACE_XBOX_HTTP_CLIENT_END
