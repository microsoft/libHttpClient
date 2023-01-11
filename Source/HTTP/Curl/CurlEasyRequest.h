#pragma once

#if HC_PLATFORM == HC_PLATFORM_GDK
// When developing titles for Xbox consoles, you must use WinHTTP or xCurl.
// See https://docs.microsoft.com/en-us/gaming/gdk/_content/gc/networking/overviews/web-requests/http-networking for detail
#include <XCurl.h>
#else
// This path is untested, but this http provider should work with other curl implementations as well.
// The logic in CurlMulti::Perform is optimized for XCurl, but should work on any curl implementation.
#include <curl/curl.h>
#endif

#include "Result.h"
#include "pch.h"

HRESULT HrFromCurle(CURLcode c) noexcept;
HRESULT HrFromCurlm(CURLMcode c) noexcept;

namespace xbox
{
namespace httpclient
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

} // httpclient
} // xbox
