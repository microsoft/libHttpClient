// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

log_entry::log_entry(_In_ HC_LOG_LEVEL level, _In_ std::string category) :
    m_logLevel(level),
    m_category(std::move(category))
{

}

log_entry::log_entry(_In_ HC_LOG_LEVEL level, _In_ std::string category, _In_ std::string msg) :
    m_logLevel(level),
    m_category(std::move(category))
{
    m_message << msg;
}

std::string log_entry::level_to_string() const
{
    switch (m_logLevel)
    {
        case HC_LOG_LEVEL::LOG_ERROR: return "error";
        case HC_LOG_LEVEL::LOG_OFF: return "off";
        case HC_LOG_LEVEL::LOG_VERBOSE: return "verbose";
    }

    return "";
}


NAMESPACE_XBOX_HTTP_CLIENT_LOG_END
