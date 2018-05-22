#include "pch.h"

#include "httpClient/pal.h"
#include <httpClient/httpClient.h>

HRESULT Internal_HCHttpPlatformInitialize(void* context, HCPlatformContext** platformContext)
{
    JavaVM* javaVm = reinterpret_cast<JavaVM*>(context);
    JNIEnv* jniEnv = nullptr;
    // Java classes can only be resolved when we are on a Java-initiated thread. When we are on
    // a C++ background thread and attach to Java we do not have the full class-loader information.
    // This call should be made on JNI_OnLoad or another java thread and we will cache a global reference
    // to the classes we will use for making HTTP requests.
    jint result = javaVm->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

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

    jclass globalRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    jclass globalResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));

    *platformContext = new HCPlatformContext(javaVm, globalRequestClass, globalResponseClass);
    return S_OK;
}

HCPlatformContext::HCPlatformContext(JavaVM* javaVm, jclass requestClass, jclass responseClass) :
    m_javaVm { javaVm },
    m_httpRequestClass { requestClass },
    m_httpResponseClass { responseClass }
{
}

HCPlatformContext::~HCPlatformContext()
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "~HCPlatformContext");
    JNIEnv* jniEnv = nullptr;
    bool isThreadAttached = false;
    jint getEnvResult = m_javaVm->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (getEnvResult == JNI_EDETACHED)
    {
        jint attachThreadResult = m_javaVm->AttachCurrentThread(&jniEnv, nullptr);

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
        jniEnv->DeleteGlobalRef(m_httpRequestClass);        
        jniEnv->DeleteGlobalRef(m_httpResponseClass);        
    }

    if (isThreadAttached)
    {
        m_javaVm->DetachCurrentThread();
    }
}
