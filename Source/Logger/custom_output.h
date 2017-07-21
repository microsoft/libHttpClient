// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "log.h"

NAMESPACE_XBOX_LIBHCBEGIN

class custom_output: public log_output
{
public:
    custom_output() : log_output(log_output_level_setting::use_logger_setting, HC_LOG_LEVEL::LOG_OFF) {}

    void add_log(_In_ const log_entry& entry) override;
};

NAMESPACE_XBOX_LIBHCEND
