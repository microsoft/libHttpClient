// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "threadpool.h"
#include "asyncop.h"
#include "mem.h"

/// <summary>
/// Enumeration values that indicate the trace levels of debug output for service diagnostics.
///
/// Setting the debug trace level to error or higher reports the last HRESULT, the current
/// function, the source file, and the line number for many trace points in the Xbox live code.
/// </summary>
enum class xbox_services_diagnostics_trace_level
{
    /// <summary>
    /// Output no tracing and debugging messages.
    /// </summary>
    off,

    /// <summary>
    /// Output error-handling messages.
    /// </summary>
    error,

    /// <summary>
    /// Output warnings and error-handling messages.
    /// </summary>
    warning,

    /// <summary>
    /// Output informational messages, warnings, and error-handling messages.
    /// </summary>
    info,

    /// <summary>
    /// Output all debugging and tracing messages.
    /// </summary>
    verbose
};

std::string to_utf8string(std::string value);

std::string to_utf8string(const std::wstring &value);

std::wstring to_utf16string(const std::string &value);

std::wstring to_utf16string(std::wstring value);
