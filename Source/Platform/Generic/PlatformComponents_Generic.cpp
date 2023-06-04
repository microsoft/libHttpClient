#include "pch.h"
#include "Platform/PlatformComponents.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    UNREFERENCED_PARAMETER(components);
    UNREFERENCED_PARAMETER(initArgs);

    // Note that this will cause an assert if the providers are ever used
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END