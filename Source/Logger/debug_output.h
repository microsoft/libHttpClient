// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "log.h"

NAMESPACE_XBOX_LIBHCBEGIN

class debug_output : public log_output
{
public:
    debug_output() : log_output(log_output_level_setting::use_logger_setting, log_level::off) {}

    void write(_In_ const std::string& msg) override;
};

NAMESPACE_XBOX_LIBHCEND
