// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "singleton.h"
#include "custom_output.h"

NAMESPACE_XBOX_LIBHCBEGIN

void custom_output::add_log(_In_ const log_entry& entry)
{
    get_http_singleton()->_Raise_logging_event(entry.get_log_level(), entry.category(), entry.msg_stream().str());
}

NAMESPACE_XBOX_LIBHCEND
