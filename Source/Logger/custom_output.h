// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

class custom_output: public log_output
{
public:
    custom_output() : log_output() {}

    void add_log(_In_ const log_entry& entry) override;
};

NAMESPACE_XBOX_HTTP_CLIENT_LOG_END
