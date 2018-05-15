#include "pch.h"

#include "android_http_request.h"

#include <httpClient/httpClient.h>

/* static */ JavaVM* HttpRequest::s_javaVM = nullptr;
/* static */ jclass HttpRequest::s_httpRequestClass = nullptr;
/* static */ jclass HttpRequest::s_httpResponseClass = nullptr;

/* static */ HRESULT HttpRequest::InitializeJavaEnvironment(JavaVM* javaVM) {
    // TODO: Return an HRESULT or another error type?
    s_javaVM = javaVM;
    JNIEnv* jniEnv = nullptr;
    jint threadAttached = s_javaVM->AttachCurrentThread(&jniEnv, nullptr);

    // TODO: Check that the thread is attached.
    // TODO: Check that class was found.

    // Java classes can only be resolved when we are on a Java-initiated thread; when we are on
    // a C++ background thread and attach to Java we do not have the full class-loader information.
    // This call should be made on JNI_Onload or another java thread and we will cache a global reference
    // to the classes we will use for making HTTP requests.
    jclass localHttpRequest = jniEnv->FindClass("com/xbox/httpclient/HttpClientRequest");

    if (localHttpRequest == nullptr) {
        return E_FAIL;
    }

    jclass localHttpResponse = jniEnv->FindClass("com/xbox/httpclient/HttpClientResponse");

    if (localHttpResponse == nullptr) {
        return E_FAIL;
    }

    s_httpRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    s_httpResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));   

    HC_TRACE_INFORMATION(HTTPCLIENT, "Successfully initialized java environment. %d %d", s_httpRequestClass, s_httpResponseClass);
    
//    s_javaVM->DetachCurrentThread();

    return S_OK;
}

/* static */ void HttpRequest::CleanupJavaEnvironment() {
    JNIEnv* jniEnv = nullptr;
    jint threadAttached = s_javaVM->AttachCurrentThread(&jniEnv, nullptr);

    jniEnv->DeleteGlobalRef(s_httpRequestClass);
    jniEnv->DeleteGlobalRef(s_httpResponseClass);

    s_javaVM->DetachCurrentThread();
}

HttpRequest::HttpRequest() : m_jniEnv(nullptr), m_httpRequestInstance(nullptr), m_httpResponseInstance(nullptr) {
    jint threadAttached = s_javaVM->AttachCurrentThread(&m_jniEnv, nullptr);

    // TODO: move to an initialize thread so that we can return an error and push failures up the stack?
    jmethodID httpRequestCtor = m_jniEnv->GetMethodID(s_httpRequestClass, "<init>", "()V");
    m_httpRequestInstance = m_jniEnv->NewObject(s_httpRequestClass, httpRequestCtor);
}

void HttpRequest::SetUrl(const char* url)
{
    jmethodID httpRequestSetUrlMethod = m_jniEnv->GetMethodID(s_httpRequestClass, "setHttpUrl", "(Ljava/lang/String;)V");
    jstring urlJstr = m_jniEnv->NewStringUTF(url);
    m_jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetUrlMethod, urlJstr);

    m_jniEnv->DeleteLocalRef(urlJstr);
}

void HttpRequest::AddHeader(const char* headerName, const char* headerValue)
{
    jmethodID httpRequestAddHeaderMethod = m_jniEnv->GetMethodID(s_httpRequestClass, "setHttpHeader", "(Ljava/lang/String;Ljava/lang/String;)V");
    jstring nameJstr = m_jniEnv->NewStringUTF(headerName);
    jstring valueJstr = m_jniEnv->NewStringUTF(headerValue);
    m_jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestAddHeaderMethod, nameJstr, valueJstr);

    m_jniEnv->DeleteLocalRef(nameJstr);
    m_jniEnv->DeleteLocalRef(valueJstr);
}

void HttpRequest::SetMethodAndBody(const char* method, const char* contentType, const uint8_t* body, uint32_t bodySize)
{
    jmethodID httpRequestSetBody = m_jniEnv->GetMethodID(s_httpRequestClass, "setHttpMethodAndBody", "(Ljava/lang/String;Ljava/lang/String;[B)V");
    jstring methodJstr = m_jniEnv->NewStringUTF(method);
    jstring contentTypeJstr = m_jniEnv->NewStringUTF(contentType);
    jbyteArray bodyArray = m_jniEnv->NewByteArray(bodySize);

    void *tempPrimitive = m_jniEnv->GetPrimitiveArrayCritical(bodyArray, 0);
    memcpy(tempPrimitive, body, bodySize);
    m_jniEnv->ReleasePrimitiveArrayCritical(bodyArray, tempPrimitive, 0);

    m_jniEnv->CallVoidMethod(m_httpRequestInstance, httpRequestSetBody, methodJstr, contentTypeJstr, bodyArray);

    m_jniEnv->DeleteLocalRef(methodJstr);
    m_jniEnv->DeleteLocalRef(contentTypeJstr);
}

void HttpRequest::ExecuteRequest() {
    // TODO: This can trigger a security exception if the app isn't configured for accessing the internet. Tweak the function so this can
    // be detected and bubbled back up.
    jmethodID httpRequestExecuteMethod = m_jniEnv->GetMethodID(s_httpRequestClass, "doRequest", "()Lcom/xbox/httpclient/HttpClientResponse;");
    m_httpResponseInstance = m_jniEnv->CallObjectMethod(m_httpRequestInstance, httpRequestExecuteMethod);

    jmethodID httpResponseStatusMethod = m_jniEnv->GetMethodID(s_httpResponseClass, "getResponseCode", "()I");
    jint responseStatus = m_jniEnv->CallIntMethod(m_httpResponseInstance, httpResponseStatusMethod);

//    LOGV("Response code: %d", responseStatus);
//    responseCode = (int)responseStatus;

    jmethodID httpResponssNumHeadersMethod = m_jniEnv->GetMethodID(s_httpResponseClass, "getNumHeaders", "()I");
    jint numHeaders = m_jniEnv->CallIntMethod(m_httpResponseInstance, httpResponssNumHeadersMethod);

//    LOGV("Header Count: %d", numHeaders);

    jmethodID httpRepsonseGetHeaderName = m_jniEnv->GetMethodID(s_httpResponseClass, "getHeaderNameAtIndex", "(I)Ljava/lang/String;");
    jmethodID httpRepsonseGetHeaderValue = m_jniEnv->GetMethodID(s_httpResponseClass, "getHeaderValueAtIndex", "(I)Ljava/lang/String;");
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        jstring headerName = (jstring)m_jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderName, i);
        jstring headerValue = (jstring)m_jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderValue, i);

        const char* nameCstr = m_jniEnv->GetStringUTFChars(headerName, NULL);
        const char* valueCstr = m_jniEnv->GetStringUTFChars(headerValue, NULL);

//        LOGV("Header at index %d, %s: %s", i, nameCstr, valueCstr);

        m_jniEnv->ReleaseStringUTFChars(headerName, nameCstr);
        m_jniEnv->ReleaseStringUTFChars(headerValue, valueCstr);
    }

    jmethodID httpResonseGetSize = m_jniEnv->GetMethodID(s_httpResponseClass, "getResponseBodyBytesSize", "()J");
    jlong responseSize = m_jniEnv->CallLongMethod(m_httpResponseInstance, httpResonseGetSize);
//    LOGV("Response Length: %d", (jint)responseSize);
//    m_responseSize = (uint64_t)responseSize;

    //    jmethodID httpResonseGetString = m_jniEnv->GetMethodID(m_httpResponseClass, "getResponseString", "()Ljava/lang/String;");
    //    jstring responseString = (jstring)m_jniEnv->CallObjectMethod(callResponse, httpResonseGetString);
    //    const char* responseCStr = m_jniEnv->GetStringUTFChars(responseString, NULL);
    //    LOGV("Response String: %s", responseCStr);
    //    m_jniEnv->ReleaseStringUTFChars(responseString, responseCStr);
}

void HttpRequest::ProcessResponseBody(hc_call_handle_t call) 
{
    jmethodID httpResponseBodyMethod = m_jniEnv->GetMethodID(s_httpResponseClass, "getResponseBodyBytes", "()[B");
    jbyteArray responseBody = (jbyteArray)m_jniEnv->CallObjectMethod(m_httpResponseInstance, httpResponseBodyMethod);

    int bodySize = m_jniEnv->GetArrayLength(responseBody);
    if (bodySize > 0) 
    {
        uint8_t* bodyBuffer = new uint8_t[bodySize];
        m_jniEnv->GetByteArrayRegion(responseBody, 0, bodySize, reinterpret_cast<jbyte*>(bodyBuffer));

        HCHttpCallResponseSetResponseBodyBytes(call, bodyBuffer, bodySize);
    }
}

uint32_t HttpRequest::GetResponseCode() {
    // TODO: Verify that the response completed and didn't have an error
    jmethodID httpResponseStatusMethod = m_jniEnv->GetMethodID(s_httpResponseClass, "getResponseCode", "()I");
    jint responseStatus = m_jniEnv->CallIntMethod(m_httpResponseInstance, httpResponseStatusMethod);
    return (uint32_t)responseStatus;
}

uint32_t HttpRequest::GetResponseHeaderCount() {
    // TODO: Verify that the response completed and didn't have an error
    jmethodID httpResponssNumHeadersMethod = m_jniEnv->GetMethodID(s_httpResponseClass, "getNumHeaders", "()I");
    jint numHeaders = m_jniEnv->CallIntMethod(m_httpResponseInstance, httpResponssNumHeadersMethod);
    return (uint32_t)numHeaders;
}

std::string HttpRequest::GetHeaderNameAtIndex(uint32_t index) {
    if (m_httpResponseInstance != nullptr)
    {
        jmethodID httpRepsonseGetHeaderName = m_jniEnv->GetMethodID(s_httpResponseClass, "getHeaderNameAtIndex", "(I)Ljava/lang/String;");
        jstring headerName = (jstring)m_jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderName, index);
        const char* nameCstr = m_jniEnv->GetStringUTFChars(headerName, NULL);

        std::string headerStr(nameCstr);
        m_jniEnv->ReleaseStringUTFChars(headerName, nameCstr);

        return headerStr;
    }
    else
    {
        return nullptr;
    }
}

std::string HttpRequest::GetHeaderValueAtIndex(uint32_t index) {
    if (m_httpResponseInstance != nullptr)
    {
        jmethodID httpRepsonseGetHeaderValue = m_jniEnv->GetMethodID(s_httpResponseClass, "getHeaderValueAtIndex", "(I)Ljava/lang/String;");
        jstring headerValue = (jstring)m_jniEnv->CallObjectMethod(m_httpResponseInstance, httpRepsonseGetHeaderValue, index);
        const char* valueCstr = m_jniEnv->GetStringUTFChars(headerValue, NULL);

        std::string valueStr(valueCstr);
        m_jniEnv->ReleaseStringUTFChars(headerValue, valueCstr);

        return valueStr;
    }
    else
    {
        return nullptr;
    }
}

uint64_t HttpRequest::GetResponseSize() {
    // TODO:
    return 0;
}

HttpRequest::~HttpRequest() {
    s_javaVM->DetachCurrentThread();
    m_jniEnv = nullptr;    
}