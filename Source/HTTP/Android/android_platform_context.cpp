#include "pch.h"

#include "android_platform_context.h"
#include <httpClient/httpClient.h>

Result<std::shared_ptr<AndroidPlatformContext>> AndroidPlatformContext::Initialize(HCInitArgs* args) noexcept
{
    assert(args != nullptr);
    JavaVM* javaVm = args->javaVM;
    JNIEnv* jniEnv = nullptr;

    // Pass the jvm down to XTaskQueue
    XTaskQueueSetJvm(javaVm);

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

    auto platformContext = std::shared_ptr<AndroidPlatformContext>(new (std::nothrow) AndroidPlatformContext(
        javaVm,
        args->applicationContext,
        globalRequestClass,
        globalResponseClass
    ));
    if (!platformContext) { return E_OUTOFMEMORY; }

    return std::move(platformContext);
}

AndroidPlatformContext::AndroidPlatformContext(JavaVM* javaVm, jobject applicationContext, jclass requestClass, jclass responseClass) :
    m_javaVm{ javaVm },
    m_applicationContext{ applicationContext },
    m_httpRequestClass{ requestClass },
    m_httpResponseClass{ responseClass }
{
    assert(m_javaVm != nullptr);
}

AndroidPlatformContext::~AndroidPlatformContext()
{
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
