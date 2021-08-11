// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/config.h>

#if (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)

#if defined(_WIN32)
#pragma warning(push)
#pragma warning( disable : 4005 4996 )
#if (_MSC_VER >= 1900)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)
#endif // (_MSC_VER >= 1900)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-local-typedef"
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/ssl.hpp>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#if defined(_WIN32)
#pragma warning(pop)
#endif

#include <vector>

#if HC_PLATFORM == HC_PLATFORM_ANDROID
#include <jni.h>
#include "Android/utils_android.h"
#endif

#if HC_PLATFORM_IS_APPLE
#include <CoreFoundation/CFData.h>
#include <Security/SecBase.h>
#include <Security/SecCertificate.h>
#include <Security/SecPolicy.h>
#include <Security/SecTrust.h>
#endif

#if HC_PLATFORM_IS_MICROSOFT
#include <type_traits>
#include <wincrypt.h>
#endif

namespace xbox { namespace httpclient {

static bool verify_X509_cert_chain(const http_internal_vector<http_internal_string> &certChain, const http_internal_string &hostName);

bool verify_cert_chain_platform_specific(asio::ssl::verify_context &verifyCtx, const http_internal_string &hostName)
{
    X509_STORE_CTX *storeContext = verifyCtx.native_handle();
    int currentDepth = X509_STORE_CTX_get_error_depth(storeContext);
    if (currentDepth != 0)
    {
        return true;
    }

    STACK_OF(X509) *certStack = X509_STORE_CTX_get_chain(storeContext);
    const int numCerts = sk_X509_num(certStack);
    if (numCerts < 0)
    {
        return false;
    }

    http_internal_vector<http_internal_string> certChain;
    certChain.reserve(numCerts);
    for (int i = 0; i < numCerts; ++i)
    {
        X509 *cert = sk_X509_value(certStack, i);

        // Encode into DER format into raw memory.
        int len = i2d_X509(cert, nullptr);
        if (len < 0)
        {
            return false;
        }

        http_internal_string certData;
        certData.resize(len);
        unsigned char * buffer = reinterpret_cast<unsigned char *>(&certData[0]);
        len = i2d_X509(cert, &buffer);
        if (len < 0)
        {
            return false;
        }

        certChain.push_back(std::move(certData));
    }

    auto verify_result = verify_X509_cert_chain(certChain, hostName);

    // The Windows Crypto APIs don't do host name checks, use Boost's implementation.
#if HC_PLATFORM_IS_MICROSOFT
    if (verify_result)
    {
        asio::ssl::rfc2818_verification rfc2818(hostName.data());
        verify_result = rfc2818(verify_result, verifyCtx);
    }
#endif
    return verify_result;
}

#if HC_PLATFORM == HC_PLATFORM_ANDROID
/// <summary>
/// Helper function to check return value and see if any exceptions
/// occurred when calling a JNI function.
/// <summary>
/// <returns><c>true</c> if JNI call failed, <c>false</c> otherwise.</returns>
static bool jni_failed(JNIEnv *env)
{
    if (env->ExceptionOccurred())
    {
        // Clear exception otherwise no other JNI functions can be called.
        // In the future if we improve error reporting the exception message
        // can be retrieved from here.
        env->ExceptionClear();
        return true;
    }
    return false;
}
template <typename T>
static bool jni_failed(JNIEnv *env, const java_local_ref<T> &result)
{
    if (jni_failed(env) || !result)
    {
        return true;
    }
    return false;
}
static bool jni_failed(JNIEnv *env, const jmethodID &result)
{
    if (jni_failed(env) || result == nullptr)
    {
        return true;
    }
    return false;
}
#define NO_OP do {} while(false)
#define CHECK_JREF(env, obj) if (jni_failed<decltype(obj)::element_type>(env, obj)) {return false;} NO_OP
#define CHECK_JMID(env, mid) if (jni_failed(env, mid)) {return false;} NO_OP
#define CHECK_JNI(env) if (jni_failed(env)) {return false;} NO_OP

static bool verify_X509_cert_chain(const http_internal_vector<http_internal_string> &certChain, const http_internal_string &hostName)
{
    JNIEnv* env = get_jvm_env();

    // Possible performance improvement:
    // In the future we could gain performance by turning all the jclass local
    // references into global references. Then we could lazy initialize and
    // save them globally. If this is done I'm not exactly sure where the release
    // should be.

    // ByteArrayInputStream
    java_local_ref<jclass> byteArrayInputStreamClass(env->FindClass("java/io/ByteArrayInputStream"));
    CHECK_JREF(env, byteArrayInputStreamClass);
    jmethodID byteArrayInputStreamConstructorMethod = env->GetMethodID(
        byteArrayInputStreamClass.get(),
        "<init>",
        "([B)V");
    CHECK_JMID(env, byteArrayInputStreamConstructorMethod);

    // CertificateFactory
    java_local_ref<jclass> certificateFactoryClass(env->FindClass("java/security/cert/CertificateFactory"));
    CHECK_JREF(env, certificateFactoryClass);
    jmethodID certificateFactoryGetInstanceMethod = env->GetStaticMethodID(
        certificateFactoryClass.get(),
        "getInstance",
        "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;");
    CHECK_JMID(env, certificateFactoryGetInstanceMethod);
    jmethodID generateCertificateMethod = env->GetMethodID(
        certificateFactoryClass.get(),
        "generateCertificate",
        "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;");
    CHECK_JMID(env, generateCertificateMethod);

    // X509Certificate
    java_local_ref<jclass> X509CertificateClass(env->FindClass("java/security/cert/X509Certificate"));
    CHECK_JREF(env, X509CertificateClass);

    // TrustManagerFactory
    java_local_ref<jclass> trustManagerFactoryClass(env->FindClass("javax/net/ssl/TrustManagerFactory"));
    CHECK_JREF(env, trustManagerFactoryClass);
    jmethodID trustManagerFactoryGetInstanceMethod = env->GetStaticMethodID(
        trustManagerFactoryClass.get(),
        "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;");
    CHECK_JMID(env, trustManagerFactoryGetInstanceMethod);
    jmethodID trustManagerFactoryInitMethod = env->GetMethodID(
        trustManagerFactoryClass.get(),
        "init",
        "(Ljava/security/KeyStore;)V");
    CHECK_JMID(env, trustManagerFactoryInitMethod);
    jmethodID trustManagerFactoryGetTrustManagersMethod = env->GetMethodID(
        trustManagerFactoryClass.get(),
        "getTrustManagers",
        "()[Ljavax/net/ssl/TrustManager;");
    CHECK_JMID(env, trustManagerFactoryGetTrustManagersMethod);

    // X509TrustManager
    java_local_ref<jclass> X509TrustManagerClass(env->FindClass("javax/net/ssl/X509TrustManager"));
    CHECK_JREF(env, X509TrustManagerClass);
    jmethodID X509TrustManagerCheckServerTrustedMethod = env->GetMethodID(
        X509TrustManagerClass.get(),
        "checkServerTrusted",
        "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V");
    CHECK_JMID(env, X509TrustManagerCheckServerTrustedMethod);

    // RootTrustManager
    java_local_ref<jclass> RootTrustManagerClass(env->FindClass("android/security/net/config/RootTrustManager"));
    CHECK_JREF(env, RootTrustManagerClass);
    jmethodID RootTrustManagerCheckServerTrustedMethod = env->GetMethodID(
        RootTrustManagerClass.get(),
        "checkServerTrusted",
        "([Ljava/security/cert/X509Certificate;Ljava/lang/String;Ljava/lang/String;)Ljava/util/List;");
    CHECK_JMID(env, RootTrustManagerCheckServerTrustedMethod);

    // StrictHostnameVerifier
    java_local_ref<jclass> strictHostnameVerifierClass(env->FindClass("org/apache/http/conn/ssl/StrictHostnameVerifier"));
    CHECK_JREF(env, strictHostnameVerifierClass);
    jmethodID strictHostnameVerifierConstructorMethod = env->GetMethodID(strictHostnameVerifierClass.get(), "<init>", "()V");
    CHECK_JMID(env, strictHostnameVerifierConstructorMethod);
    jmethodID strictHostnameVerifierVerifyMethod = env->GetMethodID(
        strictHostnameVerifierClass.get(),
        "verify",
        "(Ljava/lang/String;Ljava/security/cert/X509Certificate;)V");
    CHECK_JMID(env, strictHostnameVerifierVerifyMethod);

    // Create CertificateFactory
    java_local_ref<jstring> XDot509String(env->NewStringUTF("X.509"));
    CHECK_JREF(env, XDot509String);
    java_local_ref<jobject> certificateFactory(env->CallStaticObjectMethod(
         certificateFactoryClass.get(),
         certificateFactoryGetInstanceMethod,
         XDot509String.get()));
    CHECK_JREF(env, certificateFactory);

    // Create Java array to store all the certs in.
    java_local_ref<jobjectArray> certsArray(env->NewObjectArray(certChain.size(), X509CertificateClass.get(), nullptr));
    CHECK_JREF(env, certsArray);

    // For each certificate perform the following steps:
    //   1. Create ByteArrayInputStream backed by DER certificate bytes
    //   2. Create Certificate using CertificateFactory.generateCertificate
    //   3. Add Certificate to array
    int i = 0;
    for(const auto &certData : certChain)
    {
        java_local_ref<jbyteArray> byteArray(env->NewByteArray(certData.size()));
        CHECK_JREF(env, byteArray);
        env->SetByteArrayRegion(byteArray.get(), 0, certData.size(), reinterpret_cast<const jbyte *>(certData.c_str()));
        CHECK_JNI(env);
        java_local_ref<jobject> byteArrayInputStream(env->NewObject(
            byteArrayInputStreamClass.get(),
            byteArrayInputStreamConstructorMethod,
            byteArray.get()));
        CHECK_JREF(env, byteArrayInputStream);

        java_local_ref<jobject> cert(env->CallObjectMethod(
            certificateFactory.get(),
            generateCertificateMethod,
            byteArrayInputStream.get()));
        CHECK_JREF(env, cert);

        env->SetObjectArrayElement(certsArray.get(), i, cert.get());
        CHECK_JNI(env);
        ++i;
    }

    // Create TrustManagerFactory, init with Android system certs
    java_local_ref<jstring> X509String(env->NewStringUTF("X509"));
    CHECK_JREF(env, X509String);
    java_local_ref<jobject> trustFactoryManager(env->CallStaticObjectMethod(
        trustManagerFactoryClass.get(),
        trustManagerFactoryGetInstanceMethod,
        X509String.get()));
    CHECK_JREF(env, trustFactoryManager);
    env->CallVoidMethod(trustFactoryManager.get(), trustManagerFactoryInitMethod, nullptr);
    CHECK_JNI(env);

    // Get TrustManager
    java_local_ref<jobjectArray> trustManagerArray(static_cast<jobjectArray>(
        env->CallObjectMethod(trustFactoryManager.get(), trustManagerFactoryGetTrustManagersMethod)));
    CHECK_JREF(env, trustManagerArray);
    java_local_ref<jobject> trustManager(env->GetObjectArrayElement(trustManagerArray.get(), 0));
    CHECK_JREF(env, trustManager);

    java_local_ref<jstring> hostNameString(env->NewStringUTF(hostName.c_str()));
    CHECK_JREF(env, hostNameString);

    // If the app has a network security manifest, checkServerTrusted will throw a CertificateException
    // indicating "Domain specific configurations require that hostname aware
    // checkServerTrusted(X509Certificate[], String, String) is used". To handle this, check if
    // the TrustManager is a RootTrustManager and call that API instead.
    bool isRootTrustManager = (bool)env->IsInstanceOf(trustManager.get(), RootTrustManagerClass.get());
    if (isRootTrustManager)
    {
        // Validate certificate chain.
        java_local_ref<jstring> RSAString(env->NewStringUTF("RSA"));
        CHECK_JREF(env, RSAString);
        env->CallObjectMethod(
            trustManager.get(),
            RootTrustManagerCheckServerTrustedMethod,
            certsArray.get(),
            RSAString.get(),
            hostNameString.get());
        CHECK_JNI(env);
    }
    else
    {
        // Validate certificate chain.
        java_local_ref<jstring> RSAString(env->NewStringUTF("RSA"));
        CHECK_JREF(env, RSAString);
        env->CallVoidMethod(
            trustManager.get(),
            X509TrustManagerCheckServerTrustedMethod,
            certsArray.get(),
            RSAString.get());
        CHECK_JNI(env);
    }

    // Verify hostname on certificate according to RFC 2818.
    java_local_ref<jobject> hostnameVerifier(env->NewObject(
        strictHostnameVerifierClass.get(), strictHostnameVerifierConstructorMethod));
    CHECK_JREF(env, hostnameVerifier);
    java_local_ref<jobject> cert(env->GetObjectArrayElement(certsArray.get(), 0));
    CHECK_JREF(env, cert);
    env->CallVoidMethod(
        hostnameVerifier.get(),
        strictHostnameVerifierVerifyMethod,
        hostNameString.get(),
        cert.get());
    CHECK_JNI(env);

    return true;
}
#endif

#if defined(__APPLE__)
namespace {
    // Simple RAII pattern wrapper to perform CFRelease on objects.
    template <typename T>
    class cf_ref
    {
    public:
        cf_ref(T v) : value(v)
        {
            static_assert(sizeof(cf_ref<T>) == sizeof(T), "Code assumes just a wrapper, see usage in CFArrayCreate below.");
        }
        cf_ref() : value(nullptr) {}
        cf_ref(cf_ref &&other) : value(other.value) { other.value = nullptr; }

        ~cf_ref()
        {
            if(value != nullptr)
            {
                CFRelease(value);
            }
        }

        T & get()
        {
            return value;
        }
    private:
        cf_ref(const cf_ref &);
        cf_ref & operator=(const cf_ref &);
        T value;
    };
}

static bool verify_X509_cert_chain(const http_internal_vector<http_internal_string> &certChain, const http_internal_string &hostName)
{
    // Build up CFArrayRef with all the certificates.
    // All this code is basically just to get into the correct structures for the Apple APIs.
    // Copies are avoided whenever possible.
    std::vector<cf_ref<SecCertificateRef>> certs;
    for(const auto & certBuf : certChain)
    {
        cf_ref<CFDataRef> certDataRef = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
                                                                   reinterpret_cast<const unsigned char*>(certBuf.c_str()),
                                                                   certBuf.size(),
                                                                   kCFAllocatorNull);
        if(certDataRef.get() == nullptr)
        {
            return false;
        }

        cf_ref<SecCertificateRef> certObj = SecCertificateCreateWithData(nullptr, certDataRef.get());
        if(certObj.get() == nullptr)
        {
            return false;
        }
        certs.push_back(std::move(certObj));
    }
    cf_ref<CFArrayRef> certsArray = CFArrayCreate(kCFAllocatorDefault, const_cast<const void **>(reinterpret_cast<void **>(&certs[0])), certs.size(), nullptr);
    if(certsArray.get() == nullptr)
    {
        return false;
    }

    // Create trust management object with certificates and SSL policy.
    // Note: SecTrustCreateWithCertificates expects the certificate to be
    // verified is the first element.
    cf_ref<CFStringRef> cfHostName = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                                                    hostName.c_str(),
                                                                    kCFStringEncodingASCII,
                                                                    kCFAllocatorNull);
    if(cfHostName.get() == nullptr)
    {
        return false;
    }
    cf_ref<SecPolicyRef> policy = SecPolicyCreateSSL(true /* client side */, cfHostName.get());
    cf_ref<SecTrustRef> trust;
    OSStatus status = SecTrustCreateWithCertificates(certsArray.get(), policy.get(), &trust.get());
    if(status == noErr)
    {
        // Perform actual certificate verification. Check for trust via return
        // value, but swallow any error messages.
        if (__builtin_available(ios 12.0, *))
        {
            if (SecTrustEvaluateWithError(trust.get(), nil))
            {
                return true;
            }
        }
        else
        {
            SecTrustResultType trustResult;
            status = SecTrustEvaluate(trust.get(), &trustResult);
            if (status == noErr && (trustResult == kSecTrustResultUnspecified || trustResult == kSecTrustResultProceed))
            {
                return true;
            }
        }
    }

    return false;
}
#endif

#if defined(_WIN32)
namespace {
    // Helper RAII unique_ptrs to free Windows structures.
    struct cert_free_certificate_context
    {
        void operator()(const CERT_CONTEXT *ctx) const
        {
            CertFreeCertificateContext(ctx);
        }
    };
    typedef std::unique_ptr<const CERT_CONTEXT, cert_free_certificate_context> cert_context;
    struct cert_free_certificate_chain
    {
        void operator()(const CERT_CHAIN_CONTEXT *chain) const
        {
            CertFreeCertificateChain(chain);
        }
    };
    typedef std::unique_ptr<const CERT_CHAIN_CONTEXT, cert_free_certificate_chain> chain_context;
}

bool verify_X509_cert_chain(const http_internal_vector<http_internal_string> &certChain, const http_internal_string &)
{
    // Create certificate context from server certificate.
    cert_context cert(CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        reinterpret_cast<const unsigned char *>(certChain[0].c_str()),
        static_cast<DWORD>(certChain[0].size())));
    if (cert == nullptr)
    {
        return false;
    }

    // Let the OS build a certificate chain from the server certificate.
    CERT_CHAIN_PARA params;
    ZeroMemory(&params, sizeof(params));
    params.cbSize = sizeof(CERT_CHAIN_PARA);
    params.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    LPSTR usages [] =
    {
        szOID_PKIX_KP_SERVER_AUTH,

        // For older servers and to match IE.
        szOID_SERVER_GATED_CRYPTO,
        szOID_SGC_NETSCAPE
    };
    params.RequestedUsage.Usage.cUsageIdentifier = std::extent<decltype(usages)>::value;
    params.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    PCCERT_CHAIN_CONTEXT chainContext;
    chain_context chain;
    if (!CertGetCertificateChain(
        nullptr,
        cert.get(),
        nullptr,
        nullptr,
        &params,
        CERT_CHAIN_REVOCATION_CHECK_CHAIN,
        nullptr,
        &chainContext))
    {
        return false;
    }
    chain.reset(chainContext);

    // Check to see if the certificate chain is actually trusted.
    if (chain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR)
    {
        return false;
    }

    return true;
}
#endif


}}

#endif
