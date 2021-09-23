#include "pch.h"
#include "winhttp_http_provider.h"

using namespace xbox::httpclient;

HRESULT Internal_InitializeHttpPlatform(HCInitArgs* args, PerformEnv& performEnv) noexcept
{
    assert(args == nullptr);
    UNREFERENCED_PARAMETER(args);

    // Mem hooked unique ptr with non-standard dtor handler in PerformEnvDeleter
    http_stl_allocator<HC_PERFORM_ENV> a{};
    performEnv.reset(new (a.allocate(1)) HC_PERFORM_ENV{});
    if (!performEnv) 
    { 
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept
{
    http_stl_allocator<HC_PERFORM_ENV> a{};
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::destroy(a, performEnv);
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::deallocate(a, performEnv, 1);
}

HRESULT Internal_SetGlobalProxy(
    _In_ HC_PERFORM_ENV* performEnv,
    _In_ const char* proxyUri
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, !performEnv || !performEnv->winHttpState);
    return performEnv->winHttpState->SetGlobalProxy(proxyUri);
}

void CALLBACK Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    ) noexcept
{
    UNREFERENCED_PARAMETER(context);
    assert(env);

    HRESULT hr = env->Perform(call, asyncBlock);
    if (FAILED(hr))
    {
        // Complete XAsyncBlock if we fail synchronously
        XAsyncComplete(asyncBlock, hr, 0);
    }
}

HC_PERFORM_ENV::HC_PERFORM_ENV() : winHttpState{ http_allocate_shared<xbox::httpclient::WinHttpState>() }
{
}

HRESULT HC_PERFORM_ENV::Perform(HCCallHandle hcCall, XAsyncBlock* async) noexcept
{
    auto httpTask = http_allocate_shared<winhttp_http_task>(async, hcCall, winHttpState, winHttpState->m_proxyType, false);
    auto raw = shared_ptr_cache::store<winhttp_http_task>(httpTask);
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !raw);
    RETURN_IF_FAILED(HCHttpCallSetContext(hcCall, raw));
    return httpTask->connect_and_send_async();
}
