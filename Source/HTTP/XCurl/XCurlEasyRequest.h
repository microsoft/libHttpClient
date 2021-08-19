#pragma once

#include <XCurl.h>
#include "Result.h"

namespace xbox
{
namespace http_client
{

class XCurlEasyRequest
{
public:
    static Result<HC_UNIQUE_PTR<XCurlEasyRequest>> Initialize(HCCallHandle hcCall, XAsyncBlock* async);
    XCurlEasyRequest(const XCurlEasyRequest&) = delete;
    ~XCurlEasyRequest();

    CURL* Handle() const noexcept;

    void Complete(CURLcode result);
    void Fail(HRESULT hr);

private:
    XCurlEasyRequest(CURL* curlEasyHandle, HCCallHandle hcCall, XAsyncBlock* async);

    // Wrapper of curl_easy_setopt
    template<typename T>
    HRESULT SetOpt(CURLoption option, T v) noexcept;

    HRESULT AddHeader(char const* name, char const* value) noexcept;
    HRESULT CopyNextBodySection(void* buffer, size_t maxSize, size_t& bytesCopied) noexcept;

    // Curl callbacks
    static size_t CALLBACK ReadCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept;
    static size_t CALLBACK WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept;
    static size_t CALLBACK WriteDataCallback(char* buffer, size_t size, size_t nmemb, void* context) noexcept;
    static int CALLBACK DebugCallback(CURL* curlHandle, curl_infotype type, char* data, size_t size, void* context) noexcept;

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
HRESULT XCurlEasyRequest::SetOpt(CURLoption option, T v) noexcept
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
