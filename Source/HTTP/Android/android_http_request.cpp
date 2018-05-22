#include "pch.h"

#include "android_http_request.h"

#include <httpClient/httpClient.h>

/* static */ JavaVM* HttpRequest::s_javaVM = nullptr;
/* static */ jclass HttpRequest::s_httpRequestClass = nullptr;
/* static */ jclass HttpRequest::s_httpResponseClass = nullptr;

/* static */ HRESULT HttpRequest::InitializeJavaEnvironment(JavaVM* javaVM) 
{
    s_javaVM = javaVM;
    JNIEnv* jniEnv = nullptr;

    // Java classes can only be resolved when we are on a Java-initiated thread. When we are on
    // a C++ background thread and attach to Java we do not have the full class-loader information.
    // This call should be made on JNI_OnLoad or another java thread and we will cache a global reference
    // to the classes we will use for making HTTP requests.
    jint result = s_javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (result != JNI_OK) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Failed to initialize because JavaVM is not attached to a java thread.");
        return E_FAIL;
    }

    jclass localHttpRequest = jniEnv->FindClass("com/xbox/httpclient/HttpClientRequest");
    if (localHttpRequest == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest class");
        // TODO: [For Pull Request]: Right now with where InitializeJavaEnvironment is called this HRESULT is never
        // bubbled all the way up to HCGlobalInitialize. Should this throw a custom exception object instead? Or
        // is there a more appropriate place to call the Java initialization function?
        return E_FAIL;
    }

    jclass localHttpResponse = jniEnv->FindClass("com/xbox/httpclient/HttpClientResponse");
    if (localHttpResponse == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientResponse class");        
        return E_FAIL;
    }

    s_httpRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    s_httpResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));
    return S_OK;
}

/* static */ void HttpRequest::CleanupJavaEnvironment() 
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "HttpRequest::CleanupJavaEnvironment");

    JNIEnv* jniEnv = nullptr;
    bool isThreadAttached = false;
    jint getEnvResult = s_javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (getEnvResult == JNI_EDETACHED) 
    {
        jint attachThreadResult = s_javaVM->AttachCurrentThread(&jniEnv, nullptr);

        if (attachThreadResult == JNI_OK) 
        {
            isThreadAttached = true;
        }
        else 
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not attach to java thread to dispose of global class references");
        }
    }

    if (jniEnv != nullptr) 
    {
        jniEnv->DeleteGlobalRef(s_httpRequestClass);
        jniEnv->DeleteGlobalRef(s_httpResponseClass);
    }

    if (isThreadAttached) 
    {
        s_javaVM->DetachCurrentThread();
    }
}

HttpRequest::HttpRequest() : m_httpRequestInstance(nullptr), m_httpResponseInstance(nullptr) 
{
}

HttpRequest::~HttpRequest()
{
}

HRESULT HttpRequest::Initialize() 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (SUCCEEDED(result)) 
    {
        jmethodID httpRequestCtor = jniEnv->GetMethodID(s_httpRequestClass, "<init>", "()V");
        if (httpRequestCtor == nullptr) 
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest constructor");
            return E_FAIL;
        }

        m_httpRequestInstance = jniEnv->NewObject(s_httpRequestClass, httpRequestCtor);
        return S_OK;
    }

    return result;
}

HRESULT HttpRequest::GetJniEnv(JNIEnv** jniEnv) 
{
    if (s_javaVM == nullptr) 
    {
        return E_HC_NOT_INITIALISED;
    }

    jint jniResult = s_javaVM->GetEnv(reinterpret_cast<void**>(jniEnv), JNI_VERSION_1_6);

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

    jmethodID httpRequestSetUrlMethod = jniEnv->GetMethodID(s_httpRequestClass, "setHttpUrl", "(Ljava/lang/String;)V");
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

    jmethodID httpRequestAddHeaderMethod = jniEnv->GetMethodID(s_httpRequestClass, "setHttpHeader", "(Ljava/lang/String;Ljava/lang/String;)V");
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

    jmethodID httpRequestSetBody = jniEnv->GetMethodID(s_httpRequestClass, "setHttpMethodAndBody", "(Ljava/lang/String;Ljava/lang/String;[B)V");
    if (httpRequestSetBody == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.setHttpMethodAndBody");
        return E_FAIL;
    }

    jstring methodJstr = jniEnv->NewStringUTF(method);
    jstring contentTypeJstr = jniEnv->NewStringUTF(contentType);
    jbyteArray bodyArray = jniEnv->NewByteArray(bodySize);

    void *tempPrimitive = jniEnv->GetPrimitiveArrayCritical(bodyArray, 0);
    memcpy(tempPrimitive, body, bodySize);
    jniEnv->ReleasePrimitiveArrayCritical(bodyArray, tempPrimitive, 0);

    jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetBody, methodJstr, contentTypeJstr, bodyArray);

    jniEnv->DeleteLocalRef(methodJstr);
    jniEnv->DeleteLocalRef(contentTypeJstr);
    return S_OK;
}

HRESULT HttpRequest::ExecuteRequest() 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (SUCCEEDED(result))
    {
        jmethodID httpRequestExecuteMethod = jniEnv->GetMethodID(s_httpRequestClass, "doRequest", "()Lcom/xbox/httpclient/HttpClientResponse;");
        if (httpRequestExecuteMethod == nullptr)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.doRequest");
            return E_FAIL;
        }

        m_httpResponseInstance = jniEnv->CallObjectMethod(m_httpRequestInstance, httpRequestExecuteMethod);

        if (m_httpResponseInstance == nullptr) 
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Http request failed");
            return E_FAIL;
        }
    }

    return result;
}

HRESULT HttpRequest::ProcessResponseBody(hc_call_handle_t call) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponseBodyMethod = jniEnv->GetMethodID(s_httpResponseClass, "getResponseBodyBytes", "()[B");
    if (httpResponseBodyMethod == nullptr) 
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest.getResponseBodyBytes");
        return E_FAIL;
    }

    jbyteArray responseBody = (jbyteArray)jniEnv->CallObjectMethod(m_httpResponseInstance, httpResponseBodyMethod);

    if (responseBody != nullptr) 
    {
        int bodySize = jniEnv->GetArrayLength(responseBody);
        if (bodySize > 0)
        {
            uint8_t* bodyBuffer = new uint8_t[bodySize];
            jniEnv->GetByteArrayRegion(responseBody, 0, bodySize, reinterpret_cast<jbyte*>(bodyBuffer));

            HCHttpCallResponseSetResponseBodyBytes(call, bodyBuffer, bodySize);
        }
    }

    return S_OK;
}

uint32_t HttpRequest::GetResponseCode() 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponseStatusMethod = jniEnv->GetMethodID(s_httpResponseClass, "getResponseCode", "()I");
    jint responseStatus = jniEnv->CallIntMethod(m_httpResponseInstance, httpResponseStatusMethod);
    return (uint32_t)responseStatus;
}

uint32_t HttpRequest::GetResponseHeaderCount() 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return result;
    }

    jmethodID httpResponssNumHeadersMethod = jniEnv->GetMethodID(s_httpResponseClass, "getNumHeaders", "()I");
    jint numHeaders = jniEnv->CallIntMethod(m_httpResponseInstance, httpResponssNumHeadersMethod);
    return (uint32_t)numHeaders;
}

std::string HttpRequest::GetHeaderNameAtIndex(uint32_t index) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return nullptr;
    }

    if (m_httpResponseInstance != nullptr)
    {
        jmethodID httpRepsonseGetHeaderName = jniEnv->GetMethodID(s_httpResponseClass, "getHeaderNameAtIndex", "(I)Ljava/lang/String;");
        jstring headerName = (jstring)jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderName, index);
        const char* nameCstr = jniEnv->GetStringUTFChars(headerName, NULL);

        std::string headerStr(nameCstr);
        jniEnv->ReleaseStringUTFChars(headerName, nameCstr);

        return headerStr;
    }
    else
    {
        return nullptr;
    }
}

std::string HttpRequest::GetHeaderValueAtIndex(uint32_t index) 
{
    JNIEnv* jniEnv = nullptr;
    HRESULT result = GetJniEnv(&jniEnv);

    if (!SUCCEEDED(result))
    {
        return nullptr;
    }

    if (m_httpResponseInstance != nullptr)
    {
        jmethodID httpRepsonseGetHeaderValue = jniEnv->GetMethodID(s_httpResponseClass, "getHeaderValueAtIndex", "(I)Ljava/lang/String;");
        jstring headerValue = (jstring)jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderValue, index);
        const char* valueCstr = jniEnv->GetStringUTFChars(headerValue, NULL);

        std::string valueStr(valueCstr);
        jniEnv->ReleaseStringUTFChars(headerValue, valueCstr);

        return valueStr;
    }
    else
    {
        return nullptr;
    }
}
