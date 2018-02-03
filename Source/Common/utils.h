// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class hc_task : public std::enable_shared_from_this<hc_task>
{
public:
    hc_task() {}

    virtual ~hc_task() {}
};

NAMESPACE_XBOX_HTTP_CLIENT_END
