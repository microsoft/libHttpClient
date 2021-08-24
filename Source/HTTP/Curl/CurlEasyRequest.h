#pragma once

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XCurl.h>
#else
// This path is untested, but this http provider should work with other Curl implementations as well
#include <curl.h>
#endif
#include "Result.h"

namespace xbox
{
namespace http_client
{

class CurlEasyRequest
{
public:
    static Result<HC_UNIQUE_PTR<CurlEasyRequest>> Initialize(HCCallHandle hcCall, XAsyncBlock* async);
    CurlEasyRequest(const CurlEasyRequest&) = delete;
    CurlEasyRequest(CurlEasyRequest&&) = delete;
    CurlEasyRequest& operator=(const CurlEasyRequest&) = delete;
    ~CurlEasyRequest();

    CURL* Handle() const noexcept;

    void Complete(CURLcode result);
    void Fail(HRESULT hr);

private:
    CurlEasyRequest(CURL* curlEasyHandle, HCCallHandle hcCall, XAsyncBlock* async);

    // Wrapper of curl_easy_setopt. Requires explicit template instantiation to ensure the correct arg is passed
    template<typename T>
    struct OptType
    {
        using type = T;
    };
    template<typename T>
    HRESULT SetOpt(CURLoption option, typename OptType<T>::type) noexcept;

    HRESULT AddHeader(char const* name, char const* value) noexcept;
    HRESULT CopyNextBodySection(void* buffer, size_t maxSize, size_t& bytesCopied) noexcept;

    // Curl callbacks
    static size_t ReadCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept;
    static size_t WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept;
    static size_t WriteDataCallback(char* buffer, size_t size, size_t nmemb, void* context) noexcept;
    static int DebugCallback(CURL* curlHandle, curl_infotype type, char* data, size_t size, void* context) noexcept;

    static HRESULT MethodStringToOpt(char const* method, CURLoption& opt) noexcept;

    CURL* m_curlEasyHandle;
    HCCallHandle m_hcCallHandle; // non-owning
    XAsyncBlock* m_asyncBlock; // non-owning

    curl_slist* m_headers{ nullptr };
    http_internal_list<http_internal_string> m_headersBuffer{};
    size_t m_bodyCopied{ 0 };
    char m_errorBuffer[CURL_ERROR_SIZE]{ 0 };
};

//------------------------------------------------------------------------------
// Template implementations
//------------------------------------------------------------------------------

template<typename T>
HRESULT CurlEasyRequest::SetOpt(CURLoption option, typename OptType<T>::type v) noexcept
{
    CURLcode result = curl_easy_setopt(m_curlEasyHandle, option, v);
    if (result != CURLE_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "curl_easy_setopt(request, %d, value) failed with %d", option, result);
    }
    return HrFromCurle(result);
}

} // http_client
} // xbox
