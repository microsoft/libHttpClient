#include "pch.h"
#include <cstring>
#include <httpClient/httpProvider.h>
#include "CurlEasyRequest.h"
#include "CurlProvider.h"

namespace xbox
{
namespace httpclient
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
    HCHttpCallRequestBodyReadFunction clientRequestBodyReadCallback{};
    size_t bodySize{};
    void* clientRequestBodyReadCallbackContext{};
    RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyReadFunction(hcCall, &clientRequestBodyReadCallback, &bodySize, &clientRequestBodyReadCallbackContext));

// Specify libcurl progress callback and create libcurl progress callback for non-GDK platforms since XCurl doesn't support libcurl progress callback
#if HC_PLATFORM != HC_PLATFORM_GDK
    // Get LHC Progress callback functions
    size_t uploadMinimumProgressInterval;
    void* uploadProgressReportCallbackContext{};
    HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetProgressReportFunction(hcCall, true, &uploadProgressReportFunction, &uploadMinimumProgressInterval, &uploadProgressReportCallbackContext));

    size_t downloadMinimumProgressInterval;
    void* downloadProgressReportCallbackContext{};
    HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
    RETURN_IF_FAILED(HCHttpCallRequestGetProgressReportFunction(hcCall, false, &downloadProgressReportFunction, &downloadMinimumProgressInterval, &downloadProgressReportCallbackContext));

    // If progress callbacks were provided by client then specify libcurl progress callback
    if (uploadProgressReportFunction != nullptr || downloadProgressReportFunction != nullptr)
    {
        easyRequest->SetOpt<void*>(CURLOPT_XFERINFODATA, easyRequest.get());
        easyRequest->SetOpt<curl_xferinfo_callback>(CURLOPT_XFERINFOFUNCTION, &ProgressReportCallback);
        easyRequest->SetOpt<long>(CURLOPT_NOPROGRESS, 0L);
    }
#endif

    // we set both POSTFIELDSIZE and INFILESIZE because curl uses one or the
    // other depending on method
    // We are allowing Setops to happen with a bodySize of zero in linux to handle certain clients
    // not being able to handle handshakes without a fixed body size.
    // The reason for an if def statement is to handle the behavioral differences in libCurl vs xCurl.

    uint64_t dynamicBodySize{};
    uint64_t dynamicBodyBytesWritten{};
    HCHttpCallRequestGetDynamicBytesWritten(hcCall, &dynamicBodySize, &dynamicBodyBytesWritten);

    bool provideBody = true;
#if HC_PLATFORM == HC_PLATFORM_GDK
    provideBody = bodySize > 0;
#endif
    if (provideBody)
    {
        if (dynamicBodySize == 0)
        {
            RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_POSTFIELDSIZE, static_cast<long>(bodySize)));
            RETURN_IF_FAILED(easyRequest->SetOpt<long>(CURLOPT_INFILESIZE, static_cast<long>(bodySize)));
        }

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

#if HC_PLATFORM == HC_PLATFORM_GDK
    size_t uploadMinimumProgressInterval;
    void* uploadProgressReportCallbackContext{};
    HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    hr = HCHttpCallRequestGetProgressReportFunction(request->m_hcCallHandle, true, &uploadProgressReportFunction, &uploadMinimumProgressInterval, &uploadProgressReportCallbackContext);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ReadCallback: failed getting Progress Report upload function");
        return 1;
    }

    uint64_t dynamicBodySize{};
    uint64_t dynamicBodyBytesWritten{};
    HCHttpCallRequestGetDynamicBytesWritten(request->m_hcCallHandle, &dynamicBodySize, &dynamicBodyBytesWritten);

    uint64_t reportBytesWritten = request->m_requestBodyOffset;
    uint64_t reportTotalBytes = bodySize;
    if (dynamicBodySize > 0)
    {
        reportBytesWritten = dynamicBodyBytesWritten;
        reportTotalBytes = dynamicBodySize;
    }

    ReportProgress(
        request->m_hcCallHandle,
        uploadProgressReportFunction,
        request->m_hcCallHandle->uploadMinimumProgressReportInterval,
        reportBytesWritten,
        reportTotalBytes,
        uploadProgressReportCallbackContext,
        &request->m_hcCallHandle->uploadLastProgressReport
    );
#endif

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

size_t CurlEasyRequest::WriteDataCallback(char* buffer, size_t size, size_t nmemb, void* context) noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlEasyRequest::WriteDataCallback: received data (%zu bytes)", nmemb);

    auto request = static_cast<CurlEasyRequest*>(context);

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

#if HC_PLATFORM == HC_PLATFORM_GDK
        size_t downloadMinimumProgressInterval;
        void* downloadProgressReportCallbackContext{};
        HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
        hr = HCHttpCallRequestGetProgressReportFunction(request->m_hcCallHandle, false, &downloadProgressReportFunction, &downloadMinimumProgressInterval, &downloadProgressReportCallbackContext);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::WriteDataCallback: failed getting Progress Report download function");
            return 1;
        }

        uint64_t dynamicBodySize{};
        uint64_t dynamicBodyBytesWritten{};
        HCHttpCallResponseGetDynamicBytesWritten(request->m_hcCallHandle, &dynamicBodySize, &dynamicBodyBytesWritten);

        uint64_t reportBytesWritten = request->m_responseBodySize - request->m_responseBodyRemainingToRead;
        uint64_t reportTotalBytes = request->m_responseBodySize;
        if (dynamicBodySize > 0)
        {
            reportBytesWritten = dynamicBodyBytesWritten;
            reportTotalBytes = dynamicBodySize;
        }

        ReportProgress(
            request->m_hcCallHandle,
            downloadProgressReportFunction,
            request->m_hcCallHandle->downloadMinimumProgressReportInterval,
            reportBytesWritten,
            reportTotalBytes,
            downloadProgressReportCallbackContext,
            &request->m_hcCallHandle->downloadLastProgressReport
        );
#endif
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

void CurlEasyRequest::ReportProgress(HCCallHandle call, HCHttpCallProgressReportFunction progressReportFunction, size_t minimumInterval, size_t current, size_t total, void* progressReportCallbackContext, std::chrono::steady_clock::time_point* lastProgressReport)
{
    if (progressReportFunction != nullptr)
    {
        long minimumProgressReportIntervalInMs = static_cast<long>(minimumInterval * 1000);

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastProgressReport).count();

        if (elapsed >= minimumProgressReportIntervalInMs)
        {
            HRESULT hr = progressReportFunction(call, (int)current, (int)total, progressReportCallbackContext);
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ReportProgress: something went wrong after invoking the progress callback function.");
            }

            *lastProgressReport = now;
        }
    }
}

int CurlEasyRequest::ProgressReportCallback(void* context, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) noexcept
{
    CurlEasyRequest* request = (CurlEasyRequest*)context;

    bool isUpload = ultotal > 0;
    bool isDownload = dltotal > 0;

    if (isUpload)
    {
        size_t uploadMinimumProgressInterval;
        void* uploadProgressReportCallbackContext{};
        HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
        HRESULT hr = HCHttpCallRequestGetProgressReportFunction(request->m_hcCallHandle, true, &uploadProgressReportFunction, &uploadMinimumProgressInterval, &uploadProgressReportCallbackContext);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report upload function");
            return 1;
        }

        uint64_t dynamicBodySize{};
        uint64_t dynamicBodyBytesWritten{};
        HCHttpCallRequestGetDynamicBytesWritten(request->m_hcCallHandle, &dynamicBodySize, &dynamicBodyBytesWritten);

        if (dynamicBodySize > 0)
        {
            ulnow = dynamicBodyBytesWritten;
            ultotal = dynamicBodySize;
        }

        ReportProgress(
            request->m_hcCallHandle,
            uploadProgressReportFunction,
            request->m_hcCallHandle->uploadMinimumProgressReportInterval,
            ulnow,
            ultotal,
            uploadProgressReportCallbackContext,
            &request->m_hcCallHandle->uploadLastProgressReport
        );
    }

    if (isDownload)
    {
        size_t downloadMinimumProgressInterval;
        void* downloadProgressReportCallbackContext{};
        HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
        HRESULT hr = HCHttpCallRequestGetProgressReportFunction(request->m_hcCallHandle, false, &downloadProgressReportFunction, &downloadMinimumProgressInterval, &downloadProgressReportCallbackContext);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report download function");
            return 1;
        }

        uint64_t dynamicBodySize{};
        uint64_t dynamicBodyBytesWritten{};
        HCHttpCallResponseGetDynamicBytesWritten(request->m_hcCallHandle, &dynamicBodySize, &dynamicBodyBytesWritten);

        if (dynamicBodySize > 0)
        {
            dlnow = dynamicBodyBytesWritten;
            dltotal = dynamicBodySize;
        }

        ReportProgress(
            request->m_hcCallHandle,
            downloadProgressReportFunction,
            request->m_hcCallHandle->downloadMinimumProgressReportInterval,
            dlnow,
            dltotal,
            downloadProgressReportCallbackContext,
            &request->m_hcCallHandle->downloadLastProgressReport);
    }

    return 0;
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
