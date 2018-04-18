#pragma once

// These macros define the "os"s that libHttpClient knows about
#define HC_PLATFORM_UNKNOWN 0
#define HC_PLATFORM_WIN32 1
#define HC_PLATFORM_UWA 2
#define HC_PLATFORM_XDK 3
#define HC_PLATFORM_ANDROID 11
#define HC_PLATFORM_IOS 21

// These macros define the datamodels that libHttpClient knows about
// (a datamodel defines the size of primitive types such as int and pointers)
#define HC_DATAMODEL_UNKNOWN 0
#define HC_DATAMODEL_ILP32 1 // int, long and pointer are 32 bits (32 bit platforms)
#define HC_DATAMODEL_LLP64 2 // int and long are 32 bit; long long and pointer are 64 bits (64 bit windows)
#define HC_DATAMODEL_LP64 3 // int is 32 bit; long and pointer are 64 bits (64 bit unix)
// see http://www.unix.org/version2/whatsnew/lp64_wp.html for detailed descriptions

#if defined(_WIN32)
    #include <sdkddkver.h>
    #include <winapifamily.h>

    #if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
        #define HC_PLATFORM HC_PLATFORM_WIN32
    #elif WINAPI_FAMILY == WINAPI_FAMILY_PC_APP && _WIN32_WINNT >= _WIN32_WINNT_WIN10
        #define HC_PLATFORM HC_PLATFORM_UWA
    #elif WINAPI_FAMILY == WINAPI_FAMILY_TV_APP || WINAPI_FAMILY == WINAPI_FAMILY_TV_TITLE
        #define HC_PLATFROM HC_PLATFORM_XDK
    #endif

    #if defined(_WIN64)
        #define HC_DATAMODEL HC_DATAMODEL_LLP64
    #else
        #define HC_DATAMODEL HC_DATAMODEL_ILP32
    #endif
#elif defined(__ANDROID__)
    #define HC_PLATFORM HC_PLATFORM_ANDROID

    #if defined(__LP64__)
        #define HC_DATAMODEL HC_DATAMODEL_LP64
    #else
        #define HC_DATAMODEL HC_DATAMODEL_ILP32
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE == 1
        #define HC_PLATFORM HC_PLATFORM_IOS
    #endif

    #if defined(__LP64__)
        #define HC_DATAMODEL HC_DATAMODEL_LP64
    #else
        #error HC does not support 32 bit builds for Apple platforms
    #endif
#endif

// HC_PLATFORM defines the "os" that libHttpClient is being built for
#if !defined(HC_PLATFORM)
#error HC does not recognize this platform
#define HC_PLATFORM HC_PLATFORM_UNKNOWN
#endif

// HC_PLATFORM defines the datamodel that libHttpClient is being built for
#if !defined(HC_DATAMODEL)
#error HC does not recognize the datamodel used on this platform
#define HC_DATAMODEL HC_DATAMODEL_UNKNOWN
#endif
