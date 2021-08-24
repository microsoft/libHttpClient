#include "pch.h"
#include "CurlEasyRequest.h"
#include "CurlProvider.h"

namespace xbox
{
namespace http_client
{

CurlEasyRequest::CurlEasyRequest(CURL* curlEasyHandle, HCCallHandle hcCall, XAsyncBlock* async)
    : m_curlEasyHandle{ curlEasyHandle },
    m_hcCallHandle{ hcCall },
    m_asyncBlock{ async }
{
}

CurlEasyRequest::~CurlEasyRequest()
{
    curl_easy_cleanup(m_curlEasyHandle);
    curl_slist_free_all(m_headers);
}

Result<HC_UNIQUE_PTR<CurlEasyRequest>> CurlEasyRequest::Initialize(HCCallHandle hcCall, XAsyncBlock* async)
{
    CURL* curlEasyHandle{ curl_easy_init() };
    if (!curlEasyHandle)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlEasyRequest::Initialize:: curl_easy_init failed");
        return E_FAIL;
    }

    http_stl_allocator<CurlEasyRequest> a{};
    HC_UNIQUE_PTR<CurlEasyRequest> easyRequest{ new (a.allocate(1)) CurlEasyRequest{ curlEasyHandle, hcCall, async } };

    // body (first so we can override things curl "helpfully" sets for us)
    uint8_t const* body = nullptr;
    uint32_t bodySize = 0;
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyBytes(hcCall, &body, &bodySize));

    if (bodySize > 0)
    {
        // we set both POSTFIELDSIZE and INFILESIZE because curl uses one or the
        // other depending on method
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_POSTFIELDSIZE, bodySize));
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_INFILESIZE, bodySize));

        // read callback
        RETURN_IF_FAILED(easyRequest->SetOpt<curl_read_callback>(CURLOPT_READFUNCTION, &ReadCallback));
        RETURN_IF_FAILED(easyRequest->SetOpt<void*>(CURLOPT_READDATA, easyRequest.get()));
    }

    // url & method
    char const* url = nullptr;
    char const* method = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetUrl(hcCall, &method, &url));
    RETURN_IF_FAILED(easyRequest->SetOpt<char const*>(CURLOPT_URL, url));

    CURLoption opt = CURLOPT_HTTPGET;
    RETURN_IF_FAILED(MethodStringToOpt(method, opt));
    if (opt == CURLOPT_CUSTOMREQUEST)
    {
        RETURN_IF_FAILED(easyRequest->SetOpt<char const*>(opt, method));
    }
    else
    {
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(opt, 1));
    }

    // headers
    uint32_t headerCount{ 0 };
    RETURN_IF_FAILED(HCHttpCallRequestGetNumHeaders(hcCall, &headerCount));

    for (auto i = 0u; i < headerCount; ++i)
    {
        char const* name{ nullptr };
        char const* value{ nullptr };
        RETURN_IF_FAILED(HCHttpCallRequestGetHeaderAtIndex(hcCall, i, &name, &value));
        RETURN_IF_FAILED(easyRequest->AddHeader(name, value));
    }
    RETURN_IF_FAILED(easyRequest->SetOpt<curl_slist*>(CURLOPT_HTTPHEADER, easyRequest->m_headers));

    // timeout
    uint32_t timeoutSeconds{ 0 };
    RETURN_IF_FAILED(HCHttpCallRequestGetTimeout(hcCall, &timeoutSeconds));
    RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_TIMEOUT_MS, timeoutSeconds * 1000));

    RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_VERBOSE, 0)); // verbose logging (0 off, 1 on)
    RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_HEADER, 0)); // do not write headers to the write callback
    RETURN_IF_FAILED(easyRequest->SetOpt<char*>(CURLOPT_ERRORBUFFER, easyRequest->m_errorBuffer));

    // write data callback
    RETURN_IF_FAILED(easyRequest->SetOpt<curl_write_callback>(CURLOPT_WRITEFUNCTION, &WriteDataCallback));
    RETURN_IF_FAILED(easyRequest->SetOpt<void*>(CURLOPT_WRITEDATA, easyRequest.get()));

    // write header callback
    RETURN_IF_FAILED(easyRequest->SetOpt<curl_write_callback>(CURLOPT_HEADERFUNCTION, &WriteHeaderCallback));
    RETURN_IF_FAILED(easyRequest->SetOpt<void*>(CURLOPT_HEADERDATA, easyRequest.get()));

    // debug callback
    RETURN_IF_FAILED(easyRequest->SetOpt<curl_debug_callback>(CURLOPT_DEBUGFUNCTION, &DebugCallback));

    return Result<HC_UNIQUE_PTR<CurlEasyRequest>>{ std::move(easyRequest) };
}

CURL* CurlEasyRequest::Handle() const noexcept
{
    return m_curlEasyHandle;
}

void CurlEasyRequest::Complete(CURLcode result)
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "CurlEasyRequest::Complete: CURLCode=%ul", result);

    long httpStatus = 0;
    auto getInfoRes = curl_easy_getinfo(m_curlEasyHandle, CURLINFO_RESPONSE_CODE, &httpStatus);
    if (getInfoRes != CURLE_OK)
    {
        result = getInfoRes;
    }

    if (result != CURLE_OK)
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "CurlEasyRequest::m_errorBuffer='%s'", m_errorBuffer);

        HRESULT hr = HCHttpCallResponseSetNetworkErrorCode(m_hcCallHandle, E_FAIL, result);
        assert(SUCCEEDED(hr));

        hr = HCHttpCallResponseSetPlatformNetworkErrorMessage(m_hcCallHandle, curl_easy_strerror(result));
        assert(SUCCEEDED(hr));
    }

    HRESULT hr = HCHttpCallResponseSetStatusCode(m_hcCallHandle, httpStatus);
    assert(SUCCEEDED(hr));
    UNREFERENCED_PARAMETER(hr);

    // Always complete with XAsync success; http/network errors returned via HCCallHandle
    XAsyncComplete(m_asyncBlock, S_OK, 0);
}

void CurlEasyRequest::Fail(HRESULT hr)
{
    HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::Fail");
    XAsyncComplete(m_asyncBlock, hr, 0);
}

HRESULT CurlEasyRequest::AddHeader(char const* name, char const* value) noexcept
{
    int required = std::snprintf(nullptr, 0, "%s: %s", name, value);
    assert(required > 0);

    m_headersBuffer.emplace_back();
    auto& header = m_headersBuffer.back();

    header.resize(static_cast<size_t>(required), '\0');
    int written = std::snprintf(&header[0], header.size() + 1, "%s: %s", name, value);
    assert(written == required);
    (void)written;

    m_headers = curl_slist_append(m_headers, header.c_str());

    return S_OK;
}

HRESULT CurlEasyRequest::CopyNextBodySection(void* buffer, size_t maxSize, size_t& bytesCopied) noexcept
{
    assert(buffer);

    uint8_t const* body = nullptr;
    uint32_t bodySize = 0;
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyBytes(m_hcCallHandle, &body, &bodySize));

    size_t toCopy = 0;

    if (m_bodyCopied == bodySize)
    {
        bytesCopied = 0;
        return S_OK;
    }
    else if (maxSize >= bodySize - m_bodyCopied)
    {
        // copy everything
        toCopy = bodySize - m_bodyCopied;
    }
    else
    {
        // copy as much as we can
        toCopy = maxSize;
    }

    void const* startCopyFrom = body + m_bodyCopied;
    assert(startCopyFrom < body + bodySize);

    m_bodyCopied += toCopy;
    assert(m_bodyCopied <= bodySize);

    assert(toCopy <= maxSize);
    memcpy(buffer, startCopyFrom, toCopy);

    bytesCopied = toCopy;
    return S_OK;
}

size_t CurlEasyRequest::ReadCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "CurlEasyRequest::ReadCallback: reading body data (%llu items of size %llu)", nitems, size);

    auto request = static_cast<CurlEasyRequest*>(context);

    size_t bufferSize = size * nitems;
    size_t copied = 0;
    HRESULT hr = request->CopyNextBodySection(buffer, bufferSize, copied);
    if (FAILED(hr))
    {
        return CURL_READFUNC_ABORT;
    }

    return copied;
}

size_t CurlEasyRequest::WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "CurlEasyRequest::WriteHeaderCallback: received header (%llu items of size %llu)", nitems, size);

    auto request = static_cast<CurlEasyRequest*>(context);

#if HC_TRACE_INFORMATION_ENABLE
    if (size * nitems > 2)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "'%.*s'", size * nitems - 2, buffer); // -2 to avoid printing \r\n
    }
#endif

    size_t bufferSize = size * nitems;
    char const* current = buffer;
    char const* end = buffer + bufferSize;

    // scan for the end of the header name
    char const* name = current;
    size_t nameSize = 0;
    for (; current < end; ++current)
    {
        if (*current == ':')
        {
            nameSize = current - buffer;
            ++current;
            break;
        }
    }
    if (current == end)
    {
        // not a real header, drop it
        return bufferSize;
    }

    // skip whitespace
    for (; current < end && *current == ' '; ++current) // assume that Curl canonicalizes headers
    {}

    // scan for the end of the header value
    char const* value = current;
    size_t valueSize = 0;
    char const* valueStart = current;
    for (; current < end; ++current)
    {
        if (*current == '\r')
        {
            valueSize = current - valueStart;
            break;
        }
    }
    if (current == end)
    {
        // curl should always gives us the new lines at the end of the header
        assert(false);
    }

    HRESULT hr = HCHttpCallResponseSetHeaderWithLength(request->m_hcCallHandle, name, nameSize, value, valueSize);
    assert(SUCCEEDED(hr));

    return bufferSize;
}

size_t CurlEasyRequest::WriteDataCallback(char* buffer, size_t /*size*/, size_t nmemb, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "CurlEasyRequest::WriteDataCallback: received data (%llu bytes)", nmemb);

    auto request = static_cast<CurlEasyRequest*>(context);

    HC_TRACE_INFORMATION(HTTPCLIENT, "'%.*s'", nmemb, buffer);

    HRESULT hr = HCHttpCallResponseAppendResponseBodyBytes(request->m_hcCallHandle, reinterpret_cast<uint8_t*>(buffer), nmemb);
    assert(SUCCEEDED(hr));

    return nmemb;
}

int CurlEasyRequest::DebugCallback(CURL* /*curlHandle*/, curl_infotype type, char* data, size_t size, void* /*context*/) noexcept
{
    char const* event = "<unknown>";
    switch (type)
    {
    case CURLINFO_TEXT: event = "TEXT"; break;
    case CURLINFO_HEADER_OUT: event = "HEADER OUT"; break;
    case CURLINFO_DATA_OUT: event = "DATA OUT"; break;
    case CURLINFO_SSL_DATA_OUT: event = "SSL OUT"; break;
    case CURLINFO_HEADER_IN: event = "HEADER IN"; break;
    case CURLINFO_DATA_IN: event = "DATA IN"; break;
    case CURLINFO_SSL_DATA_IN: event = "SSL IN"; break;
    case CURLINFO_END: event = "END"; break;
    }

    if (type == CURLINFO_TEXT && data[size - 1] == '\n')
    {
        size -= 1;
    }

    HC_TRACE_IMPORTANT(HTTPCLIENT, "CURL %10s - %.*s", event, size, data);

    return CURLE_OK;
}

HRESULT CurlEasyRequest::MethodStringToOpt(char const* method, CURLoption& opt) noexcept
{
    if (strcmp(method, "GET") == 0)
    {
        opt = CURLOPT_HTTPGET;
    }
    else if (strcmp(method, "POST") == 0)
    {
        opt = CURLOPT_POST;
    }
    else if (strcmp(method, "PUT") == 0)
    {
        opt = CURLOPT_UPLOAD;
    }
    else
    {
        opt = CURLOPT_CUSTOMREQUEST;
    }

    return S_OK;
}

}
}
