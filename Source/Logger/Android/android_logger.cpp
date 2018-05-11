#include <Android/log.h>
#include <httpClient/pal.h>
#include <httpClient/trace.h>

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message)
{
    int androidLogPriority = ANDROID_LOG_UNKNOWN;

    switch (traceLevel) {
        case HCTraceLevel_Off:
            androidLogPriority = ANDROID_LOG_SILENT;
            break;
        case HCTraceLevel_Error:
            androidLogPriority = ANDROID_LOG_ERROR;
            break;
        case HCTraceLevel_Warning:
            androidLogPriority = ANDROID_LOG_WARN;
            break;
        case HCTraceLevel_Important:
            androidLogPriority = ANDROID_LOG_WARN;
            break;
        case HCTraceLevel_Information:
            androidLogPriority = ANDROID_LOG_INFO;
            break;
        case HCTraceLevel_Verbose:
            androidLogPriority = ANDROID_LOG_VERBOSE;
            break;
    }

    __android_log_print(androidLogPriority, areaName, "%s", message);
}