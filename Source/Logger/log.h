// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "utils.h"
#include <string>
#include <sstream>
#include <iostream>
#include "singleton.h"

#pragma once

#define DEFAULT_LOGGER get_http_singleton()->m_logger
#define IF_LOGGER_ENABLED(logger) if(logger != nullptr)
#define IF_LOG_LEVEL_ENABLED(logger, level) if(logger != nullptr && logger->log_level_enabled(level))

#define LOG(logger, level, category, msg) IF_LOGGER_ENABLED(logger) logger->add_log(xbox::httpclient::log::log_entry(level, category, msg))
#define LOGS(logger, level, category) IF_LOGGER_ENABLED(logger) *logger += xbox::httpclient::log::log_entry(level, category)

const char defaultCategory[] = "";
#define IF_LOG_ERROR() IF_LOG_LEVEL_ENABLED(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_ERROR)
#define LOG_ERROR(msg) IF_LOG_ERROR() LOG(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_ERROR, defaultCategory, msg)
#define LOG_ERROR_IF(boolean_expression, msg) if(boolean_expression) LOG_ERROR(msg)
#define LOGS_ERROR IF_LOG_ERROR() LOGS(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_ERROR, defaultCategory)
#define LOGS_ERROR_IF(boolean_expression) if(boolean_expression) LOGS_ERROR

#define IF_LOG_INFO() IF_LOG_LEVEL_ENABLED(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_VERBOSE)
#define LOG_INFO(msg) IF_LOG_ERROR() LOG(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_VERBOSE, defaultCategory, msg)
#define LOG_INFO_IF(boolean_expression, msg) if(boolean_expression) LOG_INFO(msg)
#define LOGS_INFO IF_LOG_INFO() LOGS(DEFAULT_LOGGER, HC_LOG_LEVEL::LOG_VERBOSE, defaultCategory)
#define LOGS_INFO_IF(boolean_expression) if(boolean_expression) LOGS_INFO

NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN

class log_entry
{
public:
    log_entry(_In_ HC_LOG_LEVEL level, _In_ std::string category);
    log_entry(_In_ HC_LOG_LEVEL level, _In_ std::string category, _In_ std::string msg);

    std::string level_to_string() const;
    const std::stringstream& msg_stream() const { return m_message; }
    const std::string& category() const { return m_category; }
    HC_LOG_LEVEL get_log_level() const { return m_logLevel; }

    log_entry& operator<<(const char* data)
    {
        m_message << to_utf8string(data);
        return *this;
    }

    log_entry& operator<<(const std::string& data)
    {
        m_message << to_utf8string(data);
        return *this;
    }

#if !http_U
    log_entry& operator<<(const wchar_t* data)
    {
        m_message << to_utf8string(data);
        return *this;
    }

    log_entry& operator<<(const std::wstring& data)
    {
        m_message << to_utf8string(data);
        return *this;
    }
#endif

    template<typename T>
    log_entry& operator<<(const T& data)
    {
        m_message << data;
        return *this;
    }

private:
    HC_LOG_LEVEL m_logLevel;
    std::string m_category;
    std::stringstream m_message;
};

enum log_output_level_setting
{
    use_logger_setting,
    use_own_setting
};

class log_output
{
public:
    // When log_output_type is set to use_logger_setting, the level parameter will be ignored.
    log_output();
    virtual void add_log(_In_ const log_entry& entry);

protected:
    // This function is to write the string to the final output, don't need to be thread safe.
    virtual void write(_In_ const std::string& msg);
    virtual std::string format_log(_In_ const log_entry& entry);

private:
    mutable std::mutex m_mutex;
};

class logger
{
public:
    logger() : m_logLevel(HC_LOG_LEVEL::LOG_ERROR) {}

    void set_log_level(_In_ HC_LOG_LEVEL level);
    HC_LOG_LEVEL get_log_level();
    bool log_level_enabled(_In_ HC_LOG_LEVEL level) const { return level <= m_logLevel; }
    void add_log_output(_In_ std::unique_ptr<log_output> output);
    void add_log(_In_ const log_entry& entry);
    void operator+=(_In_ const log_entry& record);

private:
    std::vector<std::unique_ptr<log_output>> m_log_outputs;
    HC_LOG_LEVEL m_logLevel;
};

NAMESPACE_XBOX_HTTP_CLIENT_LOG_END

