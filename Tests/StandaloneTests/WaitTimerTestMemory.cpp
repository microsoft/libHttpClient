// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Standalone white-box WaitTimer tests compile the private implementation into
// their executable. Its allocator helpers are deliberately hidden from the
// shared-library ABI, so these tests provide the default allocator locally.

#include "../../Source/Common/pch.h"
#include "../../Source/Global/mem.h"

#include <cstdlib>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

void* http_memory::mem_alloc(size_t size)
{
    return std::malloc(size);
}

void http_memory::mem_free(void* address)
{
    std::free(address);
}

NAMESPACE_XBOX_HTTP_CLIENT_END