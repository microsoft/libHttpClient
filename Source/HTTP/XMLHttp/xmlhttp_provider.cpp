#include "pch.h"
#include "xmlhttp_provider.h"
#include "xmlhttp_http_task.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT XmlHttpProvider::PerformAsync(
    HCCallHandle call,
    XAsyncBlock* asyncBlock
) noexcept
{
    auto httpTask = http_allocate_shared<xmlhttp_http_task>(asyncBlock, call);
    httpTask->perform_async(asyncBlock, call);
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END