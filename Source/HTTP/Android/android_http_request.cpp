#include "pch.h"

#include "android_http_request.h"
#include "http_android.h"
#include <httpClient/httpClient.h>
#include <vector>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// The following symbols live in files that are not referenced by any other C
// file and are only called by JNI callers. In order to force the compiler to
// not ignore the file we make a token dummy reference to the file here.
volatile auto dummyHCRequestOnRequestCompleted = &Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted;
volatile auto dummyHCRequestOnRequestFailed = &Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed;

HttpRequest::HttpRequest(
    XAsyncBlock* asyncBlock,
    JavaVM* javaVm,
    jobject applicationContext,
    jclass httpRequestClass,
    jclass httpResponseClass
) :
    m_httpRequestInstance(nullptr), 
    m_asyncBlock(asyncBlock),
    m_javaVm(javaVm),
    m_applicationContext(applicationContext),
    m_httpRequestClass(httpRequestClass),
    m_httpResponseClass(httpResponseClass)
{
    assert(m_javaVm);
}

HttpRequest::~HttpRequest()
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);
    
    if (SUCCEEDED(result) && m_httpRequestInstance != nullptr)
    {
        jniEnv->DeleteGlobalRef(m_httpRequestInstance);
        m_httpRequestInstance = nullptr;
    }
}

HRESULT HttpRequest::Initialize() 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (SUCCEEDED(result)) 
    {
        jmethodID httpRequestCtor = jniEnv->GetMethodID(m_httpRequestClass, "<init>", "(Landroid/content/Context;)V");
        if (httpRequestCtor == nullptr) 
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest constructor");
            return E_FAIL;
        }

        jobject requestInstance = jniEnv->NewObject(m_httpRequestClass, httpRequestCtor, m_applicationContext);
        m_httpRequestInstance = jniEnv->NewGlobalRef(requestInstance);
        jniEnv->DeleteLocalRef(requestInstance);

        return S_OK;
    }

    return result;
}

HRESULT HttpRequest::GetJniEnv(JNIEnv** jniEnv) 
{
    if (m_javaVm == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "javaVm is null");
        return E_HC_NOT_INITIALISED;
    }

    jint jniResult = m_javaVm->GetEnv(reinterpret_cast<void**>(jniEnv), JNI_VERSION_1_6);

    if (jniResult != JNI_OK) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not initialize HTTP request object, JavaVM is not attached to a java thread. %d", jniResult);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT HttpRequest::SetUrl(const char* url)
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpRequestSetUrlMethod = jniEnv->GetMethodID(m_httpRequestClass, "setHttpUrl", "(Ljava/lang/String;)V");
    if (httpRequestSetUrlMethod == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.setHttpUrl");
        return E_FAIL;
    }

    jstring urlJstr = jniEnv->NewStringUTF(url);
    jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetUrlMethod, urlJstr);

    jniEnv->DeleteLocalRef(urlJstr);
    return S_OK;
}

HRESULT HttpRequest::AddHeader(const char* headerName, const char* headerValue)
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpRequestAddHeaderMethod = jniEnv->GetMethodID(m_httpRequestClass, "setHttpHeader", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (httpRequestAddHeaderMethod == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.setHttpHeader");
        return E_FAIL;
    }

    jstring nameJstr = jniEnv->NewStringUTF(headerName);
    jstring valueJstr = jniEnv->NewStringUTF(headerValue);
    jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestAddHeaderMethod, nameJstr, valueJstr);

    jniEnv->DeleteLocalRef(nameJstr);
    jniEnv->DeleteLocalRef(valueJstr);
    return S_OK;
}

HRESULT HttpRequest::SetMethodAndBody(HCCallHandle call, const char* method, const char* contentType, uint32_t bodySize)
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpRequestSetBody = jniEnv->GetMethodID(m_httpRequestClass, "setHttpMethodAndBody", "(Ljava/lang/String;JLjava/lang/String;J)V");
    if (httpRequestSetBody == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.setHttpMethodAndBody");
        return E_FAIL;
    }

    jstring methodJstr = jniEnv->NewStringUTF(method);
    jstring contentTypeJstr = jniEnv->NewStringUTF(contentType);

    jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetBody, methodJstr, reinterpret_cast<jlong>(call), contentTypeJstr, static_cast<jlong>(bodySize));

    if (methodJstr != nullptr)
    {
        jniEnv->DeleteLocalRef(methodJstr);
    }

    if (contentTypeJstr != nullptr)
    {
        jniEnv->DeleteLocalRef(contentTypeJstr);
    }

    return S_OK;
}

HRESULT HttpRequest::ExecuteAsync(HCCallHandle call) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (SUCCEEDED(result))
    {
        jmethodID httpRequestExecuteAsyncMethod = jniEnv->GetMethodID(m_httpRequestClass, "doRequestAsync", "(J)V");
        if (httpRequestExecuteAsyncMethod == nullptr)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClient.doRequestAsync");
            return E_FAIL;
        }

        jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestExecuteAsyncMethod, reinterpret_cast<jlong>(call));
    }

    return result;
}

HRESULT HttpRequest::ProcessResponse(HCCallHandle call, jobject response) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponseStatusMethod = jniEnv->GetMethodID(m_httpResponseClass, "getResponseCode", "()I");
    jint responseStatus = jniEnv->CallIntMethod(response, httpResponseStatusMethod);

    HCHttpCallResponseSetStatusCode(call, (uint32_t)responseStatus);

    jmethodID httpRepsonseGetHeaderName = jniEnv->GetMethodID(m_httpResponseClass, "getHeaderNameAtIndex", "(I)Ljava/lang/String;");
    jmethodID httpRepsonseGetHeaderValue = jniEnv->GetMethodID(m_httpResponseClass, "getHeaderValueAtIndex", "(I)Ljava/lang/String;");

    for (uint32_t i = 0; i < GetResponseHeaderCount(response); i++)
    {
        jstring headerName = (jstring)jniEnv->CallObjectMethod(response, httpRepsonseGetHeaderName, i);
        jstring headerValue = (jstring)jniEnv->CallObjectMethod(response, httpRepsonseGetHeaderValue, i);
        const char* nameCstr = jniEnv->GetStringUTFChars(headerName, NULL);
        const char* valueCstr = jniEnv->GetStringUTFChars(headerValue, NULL);

        HCHttpCallResponseSetHeader(call, nameCstr, valueCstr);
        jniEnv->ReleaseStringUTFChars(headerName, nameCstr);
        jniEnv->ReleaseStringUTFChars(headerValue, valueCstr);
    }

    return ProcessResponseBody(call, response);
}

HRESULT HttpRequest::ProcessResponseBody(HCCallHandle call, jobject response)
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponseBodyMethod = jniEnv->GetMethodID(m_httpResponseClass, "getResponseBodyBytes", "()V");
    if (httpResponseBodyMethod == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.getResponseBodyBytes");
        return E_FAIL;
    }

    jniEnv->CallVoidMethod(response, httpResponseBodyMethod);
    return S_OK;
}

uint32_t HttpRequest::GetResponseHeaderCount(jobject response) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponssNumHeadersMethod = jniEnv->GetMethodID(m_httpResponseClass, "getNumHeaders", "()I");
    jint numHeaders = jniEnv->CallIntMethod(response, httpResponssNumHeadersMethod);
    return (uint32_t)numHeaders;
}

void AndroidHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv env
) noexcept
{
    auto httpSingleton = xbox::httpclient::get_http_singleton();
    if (httpSingleton == nullptr)
    {
        HCHttpCallResponseSetNetworkErrorCode(call, E_HC_NOT_INITIALISED, 0);
        XAsyncComplete(asyncBlock, E_HC_NOT_INITIALISED, 0);
        return;
    }

    std::unique_ptr<HttpRequest> httpRequest{
        new HttpRequest(
            asyncBlock,
            env->androidPlatformContext->GetJavaVm(),
            env->androidPlatformContext->GetApplicationContext(),
            env->androidPlatformContext->GetHttpRequestClass(),
            env->androidPlatformContext->GetHttpResponseClass()
        )
    };

    HRESULT result = httpRequest->Initialize();

    if (!SUCCEEDED(result))
    {
        HCHttpCallResponseSetNetworkErrorCode(call, result, 0);
        XAsyncComplete(asyncBlock, result, 0);
        return;
    }

    const char* requestUrl = nullptr;
    const char* requestMethod = nullptr;

    HCHttpCallRequestGetUrl(call, &requestMethod, &requestUrl);
    httpRequest->SetUrl(requestUrl);

    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);

    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* headerName = nullptr;
        const char* headerValue = nullptr;

        HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        httpRequest->AddHeader(headerName, headerValue);
    }

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t requestBodySize = 0;
    void* readFunctionContext = nullptr;
    HCHttpCallRequestGetRequestBodyReadFunction(call, &readFunction, &requestBodySize, &readFunctionContext);

    const char* contentType = nullptr;
    if (requestBodySize > 0)
    {
        HCHttpCallRequestGetHeader(call, "Content-Type", &contentType);
    }

    httpRequest->SetMethodAndBody(call, requestMethod, contentType, requestBodySize);

    HCHttpCallSetContext(call, httpRequest.get());
    result = httpRequest->ExecuteAsync(call);

    if (SUCCEEDED(result))
    {
        httpRequest.release();
    }
    else
    {
        XAsyncComplete(asyncBlock, E_FAIL, 0);
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END
