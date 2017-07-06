// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "threadpool.h"
#include "asyncop.h"
#include "mem.h"

#if UWP_API

class Win32Event
{
public:
    Win32Event();
    ~Win32Event();
    void Set();
    void WaitForever();

private:
    HANDLE m_event;
};

#endif