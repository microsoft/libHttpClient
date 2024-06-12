#include "pch.h"
#include <cstring>
#include <httpClient/httpProvider.h>
#include "CurlEasyRequest.h"
#include "CurlProvider.h"

namespace xbox
{
namespace httpclient
{

#if HC_PLATFORM != HC_PLATFORM_GDK
#define MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL     3000000
#define STOP_DOWNLOAD_AFTER_THIS_MANY_BYTES         6000

struct myprogress {
    curl_off_t lastruntime; /* type depends on version, see above */
    CURL* curl;
};
#endif

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
    HCHttpCallRequestBodyReadFunction clientRequestBodyReadCallback{};
    size_t bodySize{};
    void* clientRequestBodyReadCallbackContext{};
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyReadFunction(hcCall, &clientRequestBodyReadCallback, &bodySize, &clientRequestBodyReadCallbackContext));

// Specify libcurl progress callback and create libcurl progress callback for non-GDK platforms since XCurl doesn't support libcurl progress callback 
#if HC_PLATFORM != HC_PLATFORM_GDK
    // Get LHC Progress callback functions
    HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetProgressReportFunction(hcCall, true, &uploadProgressReportFunction));

    HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetProgressReportFunction(hcCall, false, &downloadProgressReportFunction));

    // If progress callbacks were provided by client then specify libcurl progress callback
    if (uploadProgressReportFunction != nullptr || downloadProgressReportFunction != nullptr)
    {
        struct myprogress prog;
        easyRequest->SetOpt<void*>(CURLOPT_XFERINFODATA, &prog);
        easyRequest->SetOpt<curl_xferinfo_callback>(CURLOPT_XFERINFOFUNCTION, &ProgressReportCallback);
        easyRequest->SetOpt<long>(CURLOPT_NOPROGRESS, 0L);
    }
#endif

    // we set both POSTFIELDSIZE and INFILESIZE because curl uses one or the
    // other depending on method
    // We are allowing Setops to happen with a bodySize of zero in linux to handle certain clients
    // not being able to handle handshakes without a fixed body size.
    // The reason for an if def statement is to handle the behavioral differences in libCurl vs xCurl.
    
#if HC_PLATFORM == HC_PLATFORM_GDK
    if (bodySize > 0)
    {
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_POSTFIELDSIZE, static_cast<long>(bodySize)));
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_INFILESIZE, static_cast<long>(bodySize)));

        // read callback
        RETURN_IF_FAILED(easyRequest->SetOpt<curl_read_callback>(CURLOPT_READFUNCTION, &ReadCallback));
        RETURN_IF_FAILED(easyRequest->SetOpt<void*>(CURLOPT_READDATA, easyRequest.get()));
    }
#else
    RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_POSTFIELDSIZE, static_cast<long>(bodySize)));
    RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_INFILESIZE, static_cast<long>(bodySize)));

    // read callback
    RETURN_IF_FAILED(easyRequest->SetOpt<curl_read_callback>(CURLOPT_READFUNCTION, &ReadCallback));
    RETURN_IF_FAILED(easyRequest->SetOpt<void*>(CURLOPT_READDATA, easyRequest.get()));
#endif

    // url & method
    char const* url = nullptr;
    char const* method = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetUrl(hcCall, &method, &url));
    RETURN_IF_FAILED(easyRequest->SetOpt<char const*>(CURLOPT_URL, url));

    CURLoption opt = CURLOPT_HTTPGET;
    RETURN_IF_FAILED(MethodStringToOpt(method, opt));
    if (opt == CURLOPT_CUSTOMREQUEST)
    {
        // Set PUT and then override as custom request. If we don't do this we Curl defaults to "GET" behavior which doesn't allow request body
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_UPLOAD, 1));
        RETURN_IF_FAILED(easyRequest->SetOpt<char const*>(opt, method));
    }
    else
    {
        RETURN_IF_FAILED(easyRequest->SetOpt<long>(opt, 1));
    }

    // headers
    uint32_t headerCount{ 0 };
    RETURN_IF_FAILED(HCHttpCallRequestGetNumHeaders(hcCall, &headerCount));

    bool haveUserAgentHeader = false;
    for (auto i = 0u; i < headerCount; ++i)
    {
        char const* name{ nullptr };
        char const* value{ nullptr };
        RETURN_IF_FAILED(HCHttpCallRequestGetHeaderAtIndex(hcCall, i, &name, &value));
        if (std::strcmp(name, "User-Agent") == 0)
        {
            haveUserAgentHeader = true;
        }
        RETURN_IF_FAILED(easyRequest->AddHeader(name, value));
    }

    if (!haveUserAgentHeader)
    {
        RETURN_IF_FAILED(easyRequest->AddHeader("User-Agent", "libHttpClient/1.0.0.0"));
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

#if HC_PLATFORM != HC_PLATFORM_GDK
void CurlEasyRequest::Perform()
{
    //// Get LHC Progress callback functions
    //HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    //HCHttpCallRequestGetProgressReportFunction(m_hcCallHandle, true, &uploadProgressReportFunction);

    //HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
    //HCHttpCallRequestGetProgressReportFunction(m_hcCallHandle, false, &downloadProgressReportFunction);

    //// If function is not null then specify libcurl progress callback
    //if (uploadProgressReportFunction != nullptr || downloadProgressReportFunction != nullptr)
    //{
        //struct myprogress prog;
        //curl_easy_setopt(m_curlEasyHandle, CURLOPT_XFERINFODATA, &prog);
        //curl_easy_setopt(m_curlEasyHandle, CURLOPT_NOPROGRESS, 0L);
        //curl_easy_setopt(m_curlEasyHandle, CURLOPT_XFERINFOFUNCTION, &CurlEasyRequest::ProgressReportCallback);
    //}

    curl_easy_setopt(m_curlEasyHandle, CURLOPT_TIMEOUT, 500);
    CURLcode result = curl_easy_perform(m_curlEasyHandle);
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::Perform Completed: CURLCode=%ul", result);
}
#endif

CURL* CurlEasyRequest::Handle() const noexcept
{
    return m_curlEasyHandle;
}

void CurlEasyRequest::Complete(CURLcode result)
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::Complete: CURLCode=%ul", result);

    if (result != CURLE_OK)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::m_errorBuffer='%s'", m_errorBuffer);

        long platformError = 0;
        auto curle = curl_easy_getinfo(m_curlEasyHandle, CURLINFO_OS_ERRNO, &platformError);
        if (curle != CURLE_OK)
        {
            return Fail(HrFromCurle(curle));
        }

        HRESULT hr = HCHttpCallResponseSetNetworkErrorCode(m_hcCallHandle, E_FAIL, static_cast<uint32_t>(platformError));
        assert(SUCCEEDED(hr));

        hr = HCHttpCallResponseSetPlatformNetworkErrorMessage(m_hcCallHandle, curl_easy_strerror(result));
        assert(SUCCEEDED(hr));
    }
    else
    {
        long httpStatus = 0;
        auto curle = curl_easy_getinfo(m_curlEasyHandle, CURLINFO_RESPONSE_CODE, &httpStatus);
        if (curle != CURLE_OK)
        {
            return Fail(HrFromCurle(curle));
        }

        HRESULT hr = HCHttpCallResponseSetStatusCode(m_hcCallHandle, httpStatus);
        assert(SUCCEEDED(hr));
        UNREFERENCED_PARAMETER(hr);
    }

    // At this point, always complete with XAsync success - http/network errors returned via HCCallHandle
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

size_t CurlEasyRequest::ReadCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::ReadCallback: reading body data (%zu items of size %zu)", nitems, size);

    auto request = static_cast<CurlEasyRequest*>(context);

    HCHttpCallRequestBodyReadFunction clientRequestBodyReadCallback{ nullptr };
    size_t bodySize{};
    void* clientRequestBodyReadCallbackContext{ nullptr };
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(request->m_hcCallHandle, &clientRequestBodyReadCallback, &bodySize, &clientRequestBodyReadCallbackContext);
    if (FAILED(hr) || !clientRequestBodyReadCallback)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlEasyRequest::ReadCallback: Unable to get client's RequestBodyRead callback");
        return CURL_READFUNC_ABORT;
    }
    
    size_t bytesWritten = 0;
    size_t bufferSize = size * nitems;
    try
    {
        hr = clientRequestBodyReadCallback(request->m_hcCallHandle, request->m_bodyCopied, bufferSize, clientRequestBodyReadCallbackContext, (uint8_t*)buffer, &bytesWritten);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ReadCallback: client RequestBodyRead callback failed");
            return CURL_READFUNC_ABORT;
        }
        request->m_bodyCopied += bytesWritten;
    }
    catch (...)
    {
        return CURL_READFUNC_ABORT;
    }

    request->m_requestBodyOffset += bytesWritten;

    ReportProgress(request, bodySize, true);

    return bytesWritten;
}

size_t CurlEasyRequest::WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* context) noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::WriteHeaderCallback: received header (%zu items of size %zu)", nitems, size);

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
    UNREFERENCED_PARAMETER(hr);

    return bufferSize;
}

size_t CurlEasyRequest::GetResponseContentLength(CURL* curlHandle)
{
    curl_off_t contentLength = 0;
    curl_easy_getinfo(curlHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
    return contentLength;
}

void CurlEasyRequest::ReportProgress(CurlEasyRequest* request, size_t bodySize, bool isUpload)
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    HCHttpCallProgressReportFunction progressReportFunction = nullptr;
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(request->m_hcCallHandle, isUpload, &progressReportFunction);
    if (FAILED(hr))
    {
        return;
    }

    if (progressReportFunction != nullptr)
    {
        uint64_t current;
        std::chrono::steady_clock::time_point lastProgressReport;
        long minimumProgressReportIntervalInMs;

        if (isUpload)
        {
            current = request->m_requestBodyOffset;
            lastProgressReport = request->m_hcCallHandle->uploadLastProgressReport;
            minimumProgressReportIntervalInMs = static_cast<long>(request->m_hcCallHandle->uploadMinimumProgressReportInterval * 1000);
        }
        else
        {
            current = bodySize - request->m_responseBodyRemainingToRead;
            lastProgressReport = request->m_hcCallHandle->downloadLastProgressReport;
            minimumProgressReportIntervalInMs = static_cast<long>(request->m_hcCallHandle->downloadMinimumProgressReportInterval * 1000);
        }

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressReport).count();

        if (elapsed >= minimumProgressReportIntervalInMs)
        {
            if (isUpload)
            {
                request->m_hcCallHandle->uploadLastProgressReport = now;
            }
            else
            {
                request->m_hcCallHandle->downloadLastProgressReport = now;
            }

            hr = progressReportFunction(request->m_hcCallHandle, current, bodySize);
            if (FAILED(hr))
            {
                return;
            }
        }
    }
#endif
}

size_t CurlEasyRequest::WriteDataCallback(char* buffer, size_t size, size_t nmemb, void* context) noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::WriteDataCallback: received data (%zu bytes)", nmemb);

    auto request = static_cast<CurlEasyRequest*>(context);

    HC_TRACE_INFORMATION(HTTPCLIENT, "'%.*s'", nmemb, buffer);

    HCHttpCallResponseBodyWriteFunction clientResponseBodyWriteCallback{ nullptr };
    void* clientResponseBodyWriteCallbackContext{ nullptr };
    HRESULT hr = HCHttpCallResponseGetResponseBodyWriteFunction(request->m_hcCallHandle, &clientResponseBodyWriteCallback, &clientResponseBodyWriteCallbackContext);
    if (FAILED(hr) || !clientResponseBodyWriteCallback)
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::WriteDataCallback: Unable to get client's ResponseBodyWrite callback");
        return 0;
    }

    size_t bufferSize = size * nmemb;
    try
    {
        hr = clientResponseBodyWriteCallback(request->m_hcCallHandle, (uint8_t*)buffer, bufferSize, clientResponseBodyWriteCallbackContext);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::WriteDataCallback: client ResponseBodyWrite callback failed");
            return 0;
        }
    }
    catch (...)
    {
        return 0;
    }

    if (!request->m_responseBodySize)
    {
        size_t contentLength = GetResponseContentLength(request->m_curlEasyHandle);

        request->m_responseBodySize = contentLength;
        request->m_responseBodyRemainingToRead = contentLength;
    }

    if (request->m_responseBodySize > 0)
    {
        request->m_responseBodyRemainingToRead -= bufferSize;
        ReportProgress(request, request->m_responseBodySize, false);
    }

    return bufferSize;
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

    HC_TRACE_VERBOSE(HTTPCLIENT, "CURL %10s - %.*s", event, size, data);

    return CURLE_OK;
}

#if HC_PLATFORM != HC_PLATFORM_GDK
int CurlEasyRequest::ProgressReportCallback(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) noexcept
{
    printf("HELLO WORLD");
    struct myprogress* myp = (struct myprogress*)p;
    CURL* curl = myp->curl;
    curl_off_t curtime = 0;

    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T, &curtime);

    /* under certain circumstances it may be desirable for certain functionality
       to only run every N seconds, in order to do this the transaction time can
       be used */
    if ((curtime - myp->lastruntime) >= MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL) {
        myp->lastruntime = curtime;
        fprintf(stderr, "TOTAL TIME: %lu.%06lu\r\n",
            (unsigned long)(curtime / 1000000),
            (unsigned long)(curtime % 1000000));
    }

    fprintf(stderr, "UP: %lu of %lu  DOWN: %lu of %lu\r\n",
        (unsigned long)ulnow, (unsigned long)ultotal,
        (unsigned long)dlnow, (unsigned long)dltotal);

    if (dlnow > STOP_DOWNLOAD_AFTER_THIS_MANY_BYTES)
        return 1;
    return 0;
}
#endif

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
    else if (strcmp(method, "HEAD") == 0)
    {
        opt = CURLOPT_NOBODY;
    }
    else
    {
        opt = CURLOPT_CUSTOMREQUEST;
    }

    return S_OK;
}

}
}
