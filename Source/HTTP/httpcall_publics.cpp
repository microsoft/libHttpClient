#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

STDAPI HCHttpCallCreate(
    _Out_ HCCallHandle* call
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call);

    auto initResult = HC_CALL::Initialize();
    RETURN_IF_FAILED(initResult.hr);

    *call = initResult.ExtractPayload().release();

    return S_OK;
}
CATCH_RETURN()


STDAPI_(HCCallHandle) HCHttpCallDuplicateHandle(
    _In_ HCCallHandle call
) noexcept
try
{
    if (call == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallDuplicateHandle [ID %llu]", TO_ULL(call->id));
    ++call->refCount;

    return call;
}
CATCH_RETURN_WITH(nullptr)

STDAPI HCHttpCallCloseHandle(
    _In_ HCCallHandle call
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call);

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCloseHandle [ID %llu]", TO_ULL(call->id));
    int refCount = --call->refCount;
    if (refCount <= 0)
    {
        ASSERT(refCount == 0); // should only fire at 0
        HC_UNIQUE_PTR<HC_CALL> reclaim{ call };
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call);

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    return httpSingleton->m_performEnv->HttpCallPerformAsyncShim(call, asyncBlock);
}
CATCH_RETURN()

STDAPI_(uint64_t) HCHttpCallGetId(
    _In_ HCCallHandle call
) noexcept
try
{
    return call ? call->id : 0;
}
CATCH_RETURN()

STDAPI HCHttpCallSetTracing(
    _In_ HCCallHandle call,
    _In_ bool logCall
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call);
    call->traceCall = logCall;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallSetContext(
    _In_ HCCallHandle call,
    _In_opt_ void* context
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call);
    call->context = context;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallGetContext(
    _In_ HCCallHandle call,
    _In_ void** context
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call || !context);
    *context = call->context;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCHttpCallGetRequestUrl(
    _In_ HCCallHandle call,
    _Out_ const char** url
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !call || !url);
    *url = call->url.data();
    return S_OK;
}
CATCH_RETURN()
