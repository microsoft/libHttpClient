// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

class console_output : public log_output
{
public:
    console_output() : log_output() {}

    void write(_In_ const std::string& msg) override;
};

NAMESPACE_XBOX_HTTP_CLIENT_LOG_END
