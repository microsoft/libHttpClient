// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HTTP/httpcall.h"
#include "buildver.h"
#include "global.h"
#include "../Mock/lhc_mock.h"

#if !HC_NOWEBSOCKETS
#include "../WebSocket/hcwebsocket.h"
#endif

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

enum class singleton_access_mode
{
    create,
    get,
    cleanup
};

HRESULT singleton_access(
    _In_ singleton_access_mode mode,
    _Out_ std::shared_ptr<http_singleton>& singleton,
    _In_opt_ HCInitArgs* createArgs = nullptr
) noexcept
{
    static std::mutex s_mutex;
    static std::shared_ptr<http_singleton> s_singleton{ nullptr };

    std::lock_guard<std::mutex> lock{ s_mutex };
    switch (mode)
    {
    case singleton_access_mode::create:
    {
        if (s_singleton)
        {
            return E_HC_ALREADY_INITIALISED;
        }

        PerformEnv performEnv;
        RETURN_IF_FAILED(Internal_InitializeHttpPlatform(createArgs, performEnv));

        s_singleton = http_allocate_shared<http_singleton>(
            GetUserHttpPerformHandler(),
#if !HC_NOWEBSOCKETS
            GetUserWebSocketPerformHandlers(),
#endif
            std::move(performEnv)
        );

        singleton = s_singleton;
        return S_OK;
    }
    case singleton_access_mode::get:
    {
        assert(!createArgs);
        singleton = s_singleton;
        return S_OK;
    }
    case singleton_access_mode::cleanup:
    {
        assert(!createArgs);

        singleton = std::move(s_singleton);
        s_singleton.reset();

        return S_OK;
    }
    default:
    {
        assert(false);
        return S_OK;
    }
    }
}

HRESULT http_singleton::create(
    _In_ HCInitArgs* args
) noexcept
{
    std::shared_ptr<http_singleton> singleton{};
    return singleton_access(singleton_access_mode::create, singleton, args);
}

HRESULT http_singleton::cleanup_async(
    _In_ XAsyncBlock* async
) noexcept
{
    std::shared_ptr<http_singleton> singleton{};
    RETURN_IF_FAILED(singleton_access(singleton_access_mode::cleanup, singleton));

    return XAsyncBegin(
        async,
        singleton.get(),
        reinterpret_cast<void*>(cleanup_async),
        __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
        {
            return XAsyncSchedule(data->async, 0);
        }
        case XAsyncOp::DoWork:
        {
            auto& self{ static_cast<http_singleton*>(data->context)->m_self };

            // Wait for all other references to the singleton to go away
            // Note that the use count check here is only valid because we never create
            // a weak_ptr to the singleton. If we did that could cause the use count
            // to increase even though we are the only strong reference
            if (self.use_count() > 1)
            {
                return XAsyncSchedule(data->async, 10);
            }

            shared_ptr_cache::cleanup(self);

            // self is the only reference at this point, the singleton will be destroyed on this thread.
            self.reset();

            XAsyncComplete(data->async, S_OK, 0);
            return S_OK;
        }
        default:
        {
            return S_OK;
        }
        }
    });
}

http_singleton::http_singleton(
    HttpPerformInfo const& httpPerformInfo,
#if !HC_NOWEBSOCKETS
    WebSocketPerformInfo const& websocketPerformInfo,
#endif
    PerformEnv&& performEnv
) :
    m_httpPerform{ httpPerformInfo },
    m_performEnv{ std::move(performEnv) }
#if !HC_NOWEBSOCKETS
    , m_websocketPerform{ websocketPerformInfo }
#endif
{}

http_singleton::~http_singleton()
{
    for (auto& mockCall : m_mocks)
    {
        HCHttpCallCloseHandle(mockCall);
    }
    m_mocks.clear();
}

std::shared_ptr<http_singleton> get_http_singleton()
{
    std::shared_ptr<http_singleton> singleton{};
    auto hr = singleton_access(singleton_access_mode::get, singleton);

    // get should never fail
    assert(SUCCEEDED(hr));

    return singleton;
}

void http_singleton::set_retry_state(
    _In_ uint32_t retryAfterCacheId,
    _In_ const http_retry_after_api_state& state)
{
    std::lock_guard<std::recursive_mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    http_retry_after_api_state oldstate = get_retry_state(retryAfterCacheId);
    if (oldstate.statusCode < 400)
    {
        m_retryAfterCache[retryAfterCacheId] = state;
    }
    else if (oldstate.retryAfterTime <= state.retryAfterTime)
    {
        m_retryAfterCache[retryAfterCacheId] = state;
    }
}

http_retry_after_api_state http_singleton::get_retry_state(_In_ uint32_t retryAfterCacheId)
{
    auto it = m_retryAfterCache.find(retryAfterCacheId); // STL is multithread read safe
    if (it != m_retryAfterCache.end())
    {
        return it->second; // returning a copy of state struct
    }

    return http_retry_after_api_state();
}

void http_singleton::clear_retry_state(_In_ uint32_t retryAfterCacheId)
{
    std::lock_guard<std::recursive_mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    m_retryAfterCache.erase(retryAfterCacheId);
}

HRESULT http_singleton::set_global_proxy(_In_ const char* proxyUri)
{
#if HC_PLATFORM == HC_PLATFORM_WIN32
    return Internal_SetGlobalProxy(m_performEnv.get(), proxyUri);
#else
    UNREFERENCED_PARAMETER(proxyUri);
    return E_NOTIMPL;
#endif
}

HttpPerformInfo& GetUserHttpPerformHandler() noexcept
{
    static HttpPerformInfo handler(&Internal_HCHttpCallPerformAsync, nullptr);
    return handler;
}

#if !HC_NOWEBSOCKETS
WebSocketPerformInfo& GetUserWebSocketPerformHandlers() noexcept
{
    static WebSocketPerformInfo handlers(
        Internal_HCWebSocketConnectAsync,
        Internal_HCWebSocketSendMessageAsync,
        Internal_HCWebSocketSendBinaryMessageAsync,
        Internal_HCWebSocketDisconnect,
        nullptr
    );
    return handlers;
}
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
