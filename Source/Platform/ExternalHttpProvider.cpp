#include "pch.h"
#include "ExternalHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

ExternalHttpProvider& ExternalHttpProvider::Get() noexcept
{
    static ExternalHttpProvider s_instance;
    return s_instance;
}

HRESULT ExternalHttpProvider::SetCallback(
    HCCallPerformFunction performFunc,
    void* context
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !performFunc);

    m_perform = performFunc;
    m_context = context;

    return S_OK;
}

HRESULT ExternalHttpProvider::GetCallback(
    HCCallPerformFunction* performFunc,
    void** context
) const noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !performFunc);
    RETURN_HR_IF(E_INVALIDARG, !context);

    *performFunc = m_perform;
    *context = m_context;

    return S_OK;
}

bool ExternalHttpProvider::HasCallback() const noexcept
{
    return m_perform;
}

HRESULT ExternalHttpProvider::PerformAsync(
    HCCallHandle callHandle,
    XAsyncBlock* async
) noexcept
{
    m_perform(callHandle, async, m_context, nullptr);
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
