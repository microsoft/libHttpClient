// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "E2EHelpers.h"

namespace e2e
{

// ------------------------------------------------------------------------------------------------
// Call
// ------------------------------------------------------------------------------------------------

Call::Call()
{
    E2E_CHECK_HR(HCHttpCallCreate(&m_call));
}

Call::~Call()
{
    if (m_call)
    {
        HCHttpCallCloseHandle(m_call);
    }
}

Call& Call::Url(const char* method, const std::string& url)
{
    E2E_CHECK_HR(HCHttpCallRequestSetUrl(m_call, method, url.c_str()));
    return *this;
}

Call& Call::Header(const char* name, const char* value)
{
    E2E_CHECK_HR(HCHttpCallRequestSetHeader(m_call, name, value, true));
    return *this;
}

Call& Call::Body(const std::string& body)
{
    E2E_CHECK_HR(HCHttpCallRequestSetRequestBodyBytes(m_call, reinterpret_cast<const uint8_t*>(body.data()), static_cast<uint32_t>(body.size())));
    return *this;
}

Call& Call::Body(const std::vector<uint8_t>& body)
{
    E2E_CHECK_HR(HCHttpCallRequestSetRequestBodyBytes(m_call, body.data(), static_cast<uint32_t>(body.size())));
    return *this;
}

Call& Call::NoRetry()
{
    E2E_CHECK_HR(HCHttpCallRequestSetRetryAllowed(m_call, false));
    return *this;
}

HRESULT Call::Perform(std::chrono::milliseconds timeout)
{
    // Heap-allocated and leaked on timeout so the in-flight operation never writes to freed memory.
    auto* async = new XAsyncBlock{};
    HRESULT hr = HCHttpCallPerformAsync(m_call, async);
    if (FAILED(hr))
    {
        delete async;
        return hr;
    }
    hr = WaitAsync(async, timeout);
    if (hr == E_PENDING)
    {
        PoisonProcess("an HTTP call never completed");
        Fail(__FILE__, __LINE__, Format("HCHttpCallPerformAsync did not complete within %lld ms", static_cast<long long>(timeout.count())));
    }
    delete async;
    return hr;
}

uint32_t Call::Status() const
{
    uint32_t status = 0;
    HCHttpCallResponseGetStatusCode(m_call, &status);
    return status;
}

HRESULT Call::NetworkError() const
{
    HRESULT net = S_OK;
    uint32_t platform = 0;
    HCHttpCallResponseGetNetworkErrorCode(m_call, &net, &platform);
    return net;
}

uint32_t Call::PlatformError() const
{
    HRESULT net = S_OK;
    uint32_t platform = 0;
    HCHttpCallResponseGetNetworkErrorCode(m_call, &net, &platform);
    return platform;
}

std::string Call::ResponseString() const
{
    const char* body = nullptr;
    HCHttpCallResponseGetResponseString(m_call, &body);
    return body ? body : "";
}

std::vector<uint8_t> Call::ResponseBytes() const
{
    size_t size = 0;
    E2E_CHECK_HR(HCHttpCallResponseGetResponseBodyBytesSize(m_call, &size));
    std::vector<uint8_t> bytes(size);
    if (size > 0)
    {
        size_t written = 0;
        E2E_CHECK_HR(HCHttpCallResponseGetResponseBodyBytes(m_call, size, bytes.data(), &written));
        bytes.resize(written);
    }
    return bytes;
}

bool Call::ResponseHeader(const char* name, std::string& value) const
{
    const char* v = nullptr;
    if (FAILED(HCHttpCallResponseGetHeader(m_call, name, &v)) || v == nullptr)
    {
        return false;
    }
    value = v;
    return true;
}

std::string Call::Describe() const
{
    const char* url = nullptr;
    HCHttpCallGetRequestUrl(m_call, &url);
    return Format("url=%s status=%u networkError=%s platformError=%u", url ? url : "?", Status(), HrToString(NetworkError()).c_str(), PlatformError());
}

std::string FetchString(const std::string& url, const char* method)
{
    Call call;
    call.Url(method, url).NoRetry();
    HRESULT hr = call.Perform();
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "%s %s: hr=%s %s", method, url.c_str(), HrToString(hr).c_str(), call.Describe().c_str());
    return call.ResponseString();
}

// ------------------------------------------------------------------------------------------------
// WebSocket
// ------------------------------------------------------------------------------------------------

WebSocket::WebSocket() : m_state{ new State{} }
{
    E2E_CHECK_HR(HCWebSocketCreate(&m_ws, OnText, OnBinary, OnClose, m_state));
}

WebSocket::~WebSocket()
{
    bool safeToFree = true;
    if (m_connected)
    {
        if (!Closed())
        {
            HCWebSocketDisconnect(m_ws);
        }
        safeToFree = WaitForClose(std::chrono::seconds{ 10 });
    }
    if (m_ws)
    {
        HCWebSocketCloseHandle(m_ws);
    }
    if (safeToFree)
    {
        delete m_state;
    }
}

void WebSocket::EnableFragmentHandler()
{
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    E2E_CHECK_HR(HCWebSocketSetBinaryMessageFragmentEventFunction(m_ws, OnFragment));
#else
    Skip("HCWebSocketSetBinaryMessageFragmentEventFunction is only available on Win32 / GDK");
#endif
}

void CALLBACK WebSocket::OnText(HCWebsocketHandle, const char* text, void* context)
{
    auto* state = static_cast<State*>(context);
    std::lock_guard<std::mutex> lock{ state->mutex };
    state->texts.emplace_back(text ? text : "");
    state->cv.notify_all();
}

void CALLBACK WebSocket::OnBinary(HCWebsocketHandle, const uint8_t* bytes, uint32_t size, void* context)
{
    auto* state = static_cast<State*>(context);
    std::lock_guard<std::mutex> lock{ state->mutex };
    state->binaries.emplace_back(bytes, bytes + size);
    state->cv.notify_all();
}

void CALLBACK WebSocket::OnFragment(HCWebsocketHandle, const uint8_t* bytes, uint32_t size, bool isLast, void* context)
{
    auto* state = static_cast<State*>(context);
    std::lock_guard<std::mutex> lock{ state->mutex };
    ++state->fragmentCallbacks;
    state->fragmentBuffer.insert(state->fragmentBuffer.end(), bytes, bytes + size);
    if (isLast)
    {
        state->fragmentedMessages.push_back(std::move(state->fragmentBuffer));
        state->fragmentBuffer.clear();
    }
    state->cv.notify_all();
}

void CALLBACK WebSocket::OnClose(HCWebsocketHandle, HCWebSocketCloseStatus status, void* context)
{
    auto* state = static_cast<State*>(context);
    std::lock_guard<std::mutex> lock{ state->mutex };
    state->closed = true;
    state->closeStatus = status;
    state->cv.notify_all();
}

HRESULT WebSocket::Connect(const std::string& url, const char* subprotocol, std::chrono::milliseconds timeout)
{
    auto* async = new XAsyncBlock{};
    HRESULT hr = HCWebSocketConnectAsync(url.c_str(), subprotocol, m_ws, async);
    if (FAILED(hr))
    {
        delete async;
        return hr;
    }
    hr = WaitAsync(async, timeout);
    if (hr == E_PENDING)
    {
        PoisonProcess("a WebSocket connect never completed");
        Fail(__FILE__, __LINE__, "HCWebSocketConnectAsync did not complete for " + url);
    }
    m_connectResult = {};
    HRESULT resultHr = HCGetWebSocketConnectResult(async, &m_connectResult);
    delete async;
    if (FAILED(hr))
    {
        return hr;
    }
    if (FAILED(resultHr))
    {
        return resultHr;
    }
    if (SUCCEEDED(m_connectResult.errorCode))
    {
        m_connected = true;
    }
    return m_connectResult.errorCode;
}

static HRESULT WaitForSend(XAsyncBlock* async, std::chrono::milliseconds timeout)
{
    HRESULT hr = WaitAsync(async, timeout);
    if (hr == E_PENDING)
    {
        PoisonProcess("a WebSocket send never completed");
        Fail(__FILE__, __LINE__, "WebSocket send did not complete");
    }
    WebSocketCompletionResult result{};
    HRESULT resultHr = HCGetWebSocketSendMessageResult(async, &result);
    delete async;
    if (FAILED(hr))
    {
        return hr;
    }
    return FAILED(resultHr) ? resultHr : result.errorCode;
}

HRESULT WebSocket::SendText(const std::string& text, std::chrono::milliseconds timeout)
{
    auto* async = new XAsyncBlock{};
    HRESULT hr = HCWebSocketSendMessageAsync(m_ws, text.c_str(), async);
    if (FAILED(hr))
    {
        delete async;
        return hr;
    }
    return WaitForSend(async, timeout);
}

HRESULT WebSocket::SendBinary(const std::vector<uint8_t>& data, std::chrono::milliseconds timeout)
{
    auto* async = new XAsyncBlock{};
    HRESULT hr = HCWebSocketSendBinaryMessageAsync(m_ws, data.data(), static_cast<uint32_t>(data.size()), async);
    if (FAILED(hr))
    {
        delete async;
        return hr;
    }
    return WaitForSend(async, timeout);
}

bool WebSocket::WaitForText(size_t count, std::chrono::milliseconds timeout)
{
    return WaitFor([&] { return m_state->texts.size() >= count; }, timeout);
}

bool WebSocket::WaitForBinary(size_t count, std::chrono::milliseconds timeout)
{
    return WaitFor([&] { return m_state->binaries.size() >= count; }, timeout);
}

bool WebSocket::WaitForFragmentedMessages(size_t count, std::chrono::milliseconds timeout)
{
    return WaitFor([&] { return m_state->fragmentedMessages.size() >= count; }, timeout);
}

bool WebSocket::WaitForClose(std::chrono::milliseconds timeout)
{
    return WaitFor([&] { return m_state->closed; }, timeout);
}

std::string WebSocket::ServerStats()
{
    size_t before = Texts().size();
    E2E_CHECK_HR(SendText("__stats__"));
    E2E_CHECK_MSG(WaitForText(before + 1), "no reply to __stats__");
    return Texts()[before];
}

std::vector<std::string> WebSocket::Texts()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->texts;
}

std::vector<std::vector<uint8_t>> WebSocket::Binaries()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->binaries;
}

std::vector<std::vector<uint8_t>> WebSocket::FragmentedMessages()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->fragmentedMessages;
}

size_t WebSocket::FragmentCallbackCount()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->fragmentCallbacks;
}

bool WebSocket::Closed()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->closed;
}

HCWebSocketCloseStatus WebSocket::CloseStatus()
{
    std::lock_guard<std::mutex> lock{ m_state->mutex };
    return m_state->closeStatus;
}

void WebSocket::DisconnectAndWait(std::chrono::milliseconds timeout)
{
    if (!Closed())
    {
        E2E_CHECK_HR(HCWebSocketDisconnect(m_ws));
    }
    E2E_CHECK_MSG(WaitForClose(timeout), "close callback not invoked after HCWebSocketDisconnect");
}

} // namespace e2e
