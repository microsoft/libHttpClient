// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

void logger::add_log_output(_In_ std::shared_ptr<log_output> output)
{
    m_log_outputs.emplace_back(output); 
};

HC_LOG_LEVEL logger::get_log_level()
{
    return m_logLevel;
}


void logger::set_log_level(_In_ HC_LOG_LEVEL level)
{
    m_logLevel = level;
}

void logger::add_log(_In_ const log_entry& logEntry)
{
    HC_LOG_LEVEL level = logEntry.get_log_level();
    if (log_level_enabled(level))
    {
        for (const auto& output : m_log_outputs)
        {
            output->add_log(logEntry);
        }
    }
}

void logger::operator+=(_In_ const log_entry& logEntry)
{
    add_log(logEntry);
}

NAMESPACE_XBOX_HTTP_CLIENT_LOG_END
