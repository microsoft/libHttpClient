#include "pch.h"

#include <android/log.h>
#include <httpClient/pal.h>
#include <httpClient/trace.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

uint64_t GetThreadId() noexcept
{
    return pthread_self();
}

void TraceToDebugger(const char* areaName, HCTraceLevel traceLevel, const char* message)
{
    int32_t androidLogPriority = ANDROID_LOG_UNKNOWN;

    switch (traceLevel) {
        case HCTraceLevel::Off:
            androidLogPriority = ANDROID_LOG_SILENT;
            break;
        case HCTraceLevel::Error:
            androidLogPriority = ANDROID_LOG_ERROR;
            break;
        case HCTraceLevel::Warning:
            androidLogPriority = ANDROID_LOG_WARN;
            break;
        case HCTraceLevel::Important:
            androidLogPriority = ANDROID_LOG_WARN;
            break;
        case HCTraceLevel::Information:
            androidLogPriority = ANDROID_LOG_INFO;
            break;
        case HCTraceLevel::Verbose:
            androidLogPriority = ANDROID_LOG_VERBOSE;
            break;
    }

    __android_log_print(androidLogPriority, areaName, "%s", message);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
