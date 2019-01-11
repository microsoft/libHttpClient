#include "pch.h"

#include <Android/log.h>
#include <httpClient/pal.h>
#include <httpClient/trace.h>

uint64_t Internal_ThisThreadId() noexcept
{
    return pthread_self();
}

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message)
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