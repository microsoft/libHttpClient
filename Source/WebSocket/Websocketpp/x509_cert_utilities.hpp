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
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <asio/ssl.hpp>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#if defined(_WIN32)
#pragma warning(pop)
#endif

#include <vector>

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

#if HC_PLATFORM == HC_PLATFORM_LINUX
#include <type_traits>
#endif

namespace xbox { namespace httpclient {

#if HC_PLATFORM == HC_PLATFORM_LINUX
    static bool verify_X509_cert_chain(asio::ssl::verify_context& verifyCtx, const http_internal_string& hostName);
#else
    static bool verify_X509_cert_chain(const http_internal_vector<http_internal_string> &certChain, const http_internal_string &hostName);
#endif

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
        unsigned char *buffer = reinterpret_cast<unsigned char*>(&certData[0]);
        len = i2d_X509(cert, &buffer);
        if (len < 0)
        {
            return false;
        }

        certChain.push_back(std::move(certData));
    }

    auto verify_result = false;

#if HC_PLATFORM == HC_PLATFORM_LINUX
    verify_result = verify_X509_cert_chain(verifyCtx, hostName);
#else
    verify_result = verify_X509_cert_chain(certChain, hostName);
#endif
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
        if (__builtin_available(iOS 12.0, macOS 10.14, *))
        {
            return SecTrustEvaluateWithError(trust.get(), nil);
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
    PCSTR usages [] =
    {
        szOID_PKIX_KP_SERVER_AUTH,

        // For older servers and to match IE.
        szOID_SERVER_GATED_CRYPTO,
        szOID_SGC_NETSCAPE
    };
    params.RequestedUsage.Usage.cUsageIdentifier = std::extent<decltype(usages)>::value;
    params.RequestedUsage.Usage.rgpszUsageIdentifier = const_cast<LPSTR*>(usages);
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

#if HC_PLATFORM == HC_PLATFORM_LINUX
static bool verify_X509_cert_chain(asio::ssl::verify_context& verifyCtx, const http_internal_string& hostName)
{
    X509_STORE_CTX* storeContext = verifyCtx.native_handle();
    int currentDepth = X509_STORE_CTX_get_error_depth(storeContext);
    if (currentDepth != 0)
    {
        return true;
    }

    STACK_OF(X509)* certStack = X509_STORE_CTX_get_chain(storeContext);
    const int numCerts = sk_X509_num(certStack);
    if (numCerts < 0)
    {
        return false;
    }

    X509_STORE* store = X509_STORE_new();
    X509_STORE_CTX_trusted_stack(storeContext, certStack);
    SSL_CTX* sslContext = SSL_CTX_new(TLS_method());
    store = SSL_CTX_get_cert_store(sslContext);

    if (sslContext == NULL) {
        return false;
    }

    int ret = X509_STORE_set_default_paths(store);
    if (ret != 1)
    {
        return false;
    }

    ret = X509_STORE_load_locations(store, NULL, "/etc/ssl/certs");
    if (ret != 1)
    {
        return false;
    }

    ret = X509_STORE_CTX_init(storeContext, store, sk_X509_value(certStack, 0), certStack);

    if (ret != 1)
    {
        return false;
    }

    ret = X509_verify_cert(storeContext);

    if (ret != 1)
    {
        return false;
    }

    return true;
}
#endif
}}

#endif
