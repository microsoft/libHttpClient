#include "pch.h"

#include "android_http_request.h"
#include <httpClient/httpClient.h>
#include <vector>

HttpRequest::HttpRequest(AsyncBlock* asyncBlock, JavaVM* javaVm, jobject applicationContext, jclass httpRequestClass, jclass httpResponseClass) :
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
        jmethodID networkAvailabilityFunc = jniEnv->GetStaticMethodID(m_httpRequestClass, "isNetworkAvailable", "(Landroid/content/Context;)Z");
        if (networkAvailabilityFunc == nullptr)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find isNetworkAvailable static method");
            return E_FAIL;
        }

        jboolean isNetworkAvailable = jniEnv->CallStaticBooleanMethod(m_httpRequestClass, networkAvailabilityFunc, m_applicationContext);
        if (!isNetworkAvailable)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not initialize HttpRequest - no network available");
            return E_HC_NO_NETWORK;
        }

        jmethodID httpRequestCtor = jniEnv->GetMethodID(m_httpRequestClass, "<init>", "()V");
        if (httpRequestCtor == nullptr) 
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest constructor");
            return E_FAIL;
        }

        jobject requestInstance = jniEnv->NewObject(m_httpRequestClass, httpRequestCtor);
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

HRESULT HttpRequest::SetMethodAndBody(const char* method, const char* contentType, const uint8_t* body, uint32_t bodySize)
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result)) 
    {
        return result;
    }

    jmethodID httpRequestSetBody = jniEnv->GetMethodID(m_httpRequestClass, "setHttpMethodAndBody", "(Ljava/lang/String;Ljava/lang/String;[B)V");
    if (httpRequestSetBody == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.setHttpMethodAndBody");
        return E_FAIL;
    }

    jstring methodJstr = jniEnv->NewStringUTF(method);
    jstring contentTypeJstr = jniEnv->NewStringUTF(contentType);
    jbyteArray bodyArray = nullptr;
    
    if (bodySize > 0)
    {
        bodyArray = jniEnv->NewByteArray(bodySize);
        void *tempPrimitive = jniEnv->GetPrimitiveArrayCritical(bodyArray, 0);
        memcpy(tempPrimitive, body, bodySize);
        jniEnv->ReleasePrimitiveArrayCritical(bodyArray, tempPrimitive, 0);
    }

    jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetBody, methodJstr, contentTypeJstr, bodyArray);

    jniEnv->DeleteLocalRef(methodJstr);

    if (bodyArray != nullptr) 
    {
        jniEnv->DeleteLocalRef(bodyArray);
    }

    if (contentTypeJstr != nullptr)
    {
        jniEnv->DeleteLocalRef(contentTypeJstr);
    }

    return S_OK;
}

HRESULT HttpRequest::ExecuteAsync(hc_call_handle_t call) 
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

HRESULT HttpRequest::ProcessResponse(hc_call_handle_t call, jobject response) 
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

HRESULT HttpRequest::ProcessResponseBody(hc_call_handle_t call, jobject repsonse) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponseBodyMethod = jniEnv->GetMethodID(m_httpResponseClass, "getResponseBodyBytes", "()[B");
    if (httpResponseBodyMethod == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.getResponseBodyBytes");
        return E_FAIL;
    }

    jbyteArray responseBody = (jbyteArray)jniEnv->CallObjectMethod(repsonse, httpResponseBodyMethod);

    if (responseBody != nullptr) 
    {
        int bodySize = jniEnv->GetArrayLength(responseBody);
        if (bodySize > 0)
        {
            http_internal_vector<uint8_t> bodyBuffer(bodySize);
            jniEnv->GetByteArrayRegion(responseBody, 0, bodySize, reinterpret_cast<jbyte*>(bodyBuffer.data()));

            HCHttpCallResponseSetResponseBodyBytes(call, bodyBuffer.data(), bodyBuffer.size());
        }
    }

    jniEnv->DeleteLocalRef(responseBody);
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

