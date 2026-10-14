// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#include <sstream>
#include <iomanip>
#include "iso8601.h"
#include "XTaskQueuePriv.h"

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

TEST_MODULE_INITIALIZE(ModuleInitialize)
{
    Logger::WriteMessage("Started unit test");
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    // Wait for task queue objects to run down before the test DLL unloads.
    // Threadpool callbacks (e.g. an async completion that already released
    // the waiting test) can still be unwinding through this module; unloading
    // under them crashes the test host.
    XTaskQueueUninitialize(5000);
    Logger::WriteMessage("Stopped unit test");
}

std::wstring FormatString(LPCWSTR strMsg, ...)
{
    WCHAR strBuffer[2048];

    va_list args;
    va_start(args, strMsg);
    StringCchVPrintfW(strBuffer, 2048, strMsg, args);
    strBuffer[2047] = L'\0';

    va_end(args);

    return std::wstring(strBuffer);
}

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
