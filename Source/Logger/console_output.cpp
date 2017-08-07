// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <stdio.h>
#include "console_output.h"


NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

void console_output::write(_In_ const std::string& msg)
{
    std::cout << msg;
}

NAMESPACE_XBOX_HTTP_CLIENT_LOG_END
