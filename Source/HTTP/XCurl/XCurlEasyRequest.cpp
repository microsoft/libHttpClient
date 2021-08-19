#include "pch.h"
#include "XCurlEasyRequest.h"
#include "XCurlProvider.h"

namespace xbox
{
namespace http_client
{

XCurlEasyRequest::XCurlEasyRequest(CURL* curlEasyHandle, HCCallHandle hcCall, XAsyncBlock* async)
    : m_curlEasyHandle{ curlEasyHandle },
    m_hcCallHandle{ hcCall },
    m_asyncBlock{ async }
{
}

XCurlEasyRequest::~XCurlEasyRequest()
{
    curl_easy_cleanup(m_curlEasyHandle);
}

Result<HC_UNIQUE_PTR<XCurlEasyRequest>> XCurlEasyRequest::Initialize(HCCallHandle hcCall, XAsyncBlock* async)
{
    CURL* curlEasyHandle{ curl_easy_init() };
    if (!curlEasyHandle)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "curl_easy_init failed");
        return E_FAIL;
    }

    http_stl_allocator<XCurlEasyRequest> a{};
    HC_UNIQUE_PTR<XCurlEasyRequest> easyRequest{ new (a.allocate(1)) XCurlEasyRequest{ curlEasyHandle, hcCall, async } };

    // body (first so we can override things curl "helpfully" sets for us)
    uint8_t const* body = nullptr;
    uint32_t bodySize = 0;
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyBytes(hcCall, &body, &bodySize));

    if (bodySize > 0)
    {
        // we set both POSTFIELDSIZE and INFILESIZE because curl uses one or the
        // other depending on method
        RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_POSTFIELDSIZE, static_cast<long>(bodySize)));
        RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_INFILESIZE, static_cast<long>(bodySize)));

        // read callback
        RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_READFUNCTION, &ReadCallback));
        RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_READDATA, easyRequest.get()));
    }

    // url & method
    char const* url = nullptr;
    char const* method = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetUrl(hcCall, &method, &url));
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_URL, url));

    CURLoption opt = CURLOPT_HTTPGET;
    RETURN_IF_FAILED(MethodStringToOpt(method, opt));
    RETURN_IF_FAILED(easyRequest->SetOpt(opt, 1));

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
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_HTTPHEADER, easyRequest->m_headers));

    // timeout
    uint32_t timeoutSeconds{ 0 };
    RETURN_IF_FAILED(HCHttpCallRequestGetTimeout(hcCall, &timeoutSeconds));
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutSeconds * 1000)));

    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_VERBOSE, 0)); // verbose logging (0 off, 1 on)
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_HEADER, 0)); // do not write headers to the write callback
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_ERRORBUFFER, easyRequest->m_errorBuffer));

    // write data callback
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_WRITEFUNCTION, &WriteDataCallback));
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_WRITEDATA, easyRequest.get()));

    // write header callback
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_HEADERFUNCTION, &WriteHeaderCallback));
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_HEADERDATA, easyRequest.get()));

    // debug callback
    RETURN_IF_FAILED(easyRequest->SetOpt(CURLOPT_DEBUGFUNCTION, &DebugCallback));

    return Result<HC_UNIQUE_PTR<XCurlEasyRequest>>{ std::move(easyRequest) };
}

CURL* XCurlEasyRequest::Handle() const noexcept
{
    return m_curlEasyHandle;
}

void XCurlEasyRequest::Complete(CURLcode result)
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "XCurlEasyRequest::Complete: CURLCode=%ul", result);

    long httpStatus = 0;
    auto getInfoRes = curl_easy_getinfo(m_curlEasyHandle, CURLINFO_RESPONSE_CODE, &httpStatus);
    if (getInfoRes != CURLE_OK)
    {
        result = getInfoRes;
    }

    if (result != CURLE_OK)
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "XCurlEasyRequest::m_errorBuffer='%s'", m_errorBuffer);

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

void XCurlEasyRequest::Fail(HRESULT hr)
{
    HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "XCurlEasyRequest::Fail");
    XAsyncComplete(m_asyncBlock, S_OK, 0);
}

HRESULT XCurlEasyRequest::AddHeader(char const* name, char const* value) noexcept
{
    int required = std::snprintf(nullptr, 0, "%s: %s", name, value);
    assert(required > 0);

    m_headersBuffer.emplace_back();
    auto& header = m_headersBuffer.back();

    header.resize(static_cast<size_t>(required) + 1, '\0');
    int written = std::snprintf(&header[0], header.size(), "%s: %s", name, value);
    assert(written == required);
    (void)written;

    header.resize(header.size() - 1); // drop null terminator

    curl_slist*& list = m_headers;
    list = curl_slist_append(list, header.c_str());

    return S_OK;
}

HRESULT XCurlEasyRequest::CopyNextBodySection(void* buffer, size_t maxSize, size_t& bytesCopied) noexcept
{
    assert(buffer);

    uint8_t const* body = nullptr;
    uint32_t bodySize = 0;
    HRESULT hr = HCHttpCallRequestGetRequestBodyBytes(m_hcCallHandle, &body, &bodySize);
    if (FAILED(hr)) { return hr; }

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

size_t CALLBACK XCurlEasyRequest::ReadCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "XCurlEasyRequest::ReadCallback: reading body data (%llu items of size %llu)", nitems, size);

    auto request = static_cast<XCurlEasyRequest*>(context);

    size_t bufferSize = size * nitems;
    size_t copied = 0;
    HRESULT hr = request->CopyNextBodySection(buffer, bufferSize, copied);
    if (FAILED(hr))
    {
        assert(false);
        return CURL_READFUNC_ABORT;
    }

    return copied;
}

size_t CALLBACK XCurlEasyRequest::WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "XCurlEasyRequest::WriteHeaderCallback: received header (%llu items of size %llu)", nitems, size);

    auto request = static_cast<XCurlEasyRequest*>(context);

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

size_t CALLBACK XCurlEasyRequest::WriteDataCallback(char* buffer, size_t /*size*/, size_t nmemb, void* context) noexcept
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "XCurlEasyRequest::WriteDataCallback: received data (%llu bytes)", nmemb);

    auto request = static_cast<XCurlEasyRequest*>(context);

    HC_TRACE_INFORMATION(HTTPCLIENT, "'%.*s'", nmemb, buffer);

    HRESULT hr = HCHttpCallResponseAppendResponseBodyBytes(request->m_hcCallHandle, reinterpret_cast<uint8_t*>(buffer), nmemb);
    assert(SUCCEEDED(hr));

    return nmemb;
}

int CALLBACK XCurlEasyRequest::DebugCallback(CURL* /*curlHandle*/, curl_infotype type, char* data, size_t size, void* /*context*/) noexcept
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

HRESULT XCurlEasyRequest::MethodStringToOpt(char const* method, CURLoption& opt) noexcept
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
        assert(false);
        return E_INVALIDARG;
    }

    return S_OK;
}

}
}