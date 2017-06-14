// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "singleton.h"
#include "asyncop.h"
#include "http_mock.h"

using namespace Windows::Foundation;
using namespace Windows::Web::Http;

void Mock_Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call
    )
{
}
