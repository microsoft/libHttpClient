#pragma once

// These macros define the "os"s that libHttpClient knows about
#define HC_PLATFORM_UNKNOWN 9999
#define HC_PLATFORM_WIN32 1
#define HC_PLATFORM_UWP 2
#define HC_PLATFORM_XDK 3
#define HC_PLATFORM_GDK 4
#define HC_PLATFORM_GRTS 5
#define HC_PLATFORM_ANDROID 11
#define HC_PLATFORM_IOS 21
#define HC_PLATFORM_MAC 22
#define HC_PLATFORM_LINUX 31
#define HC_PLATFORM_GENERIC 100
#define HC_PLATFORM_NINTENDO_SWITCH 111
#define HC_PLATFORM_SONY_PLAYSTATION_4 121
#define HC_PLATFORM_SONY_PLAYSTATION_5 131

#define HC_PLATFORM_GSDK HC_PLATFORM_GDK // For backcompat

// Set to 1 and define HC_PLATFORM_TYPES_PATH to include platform specific header
#ifndef HC_PLATFORM_HEADER_OVERRIDE
#define HC_PLATFORM_HEADER_OVERRIDE 0
#endif

// These macros define the datamodels that libHttpClient knows about
// (a datamodel defines the size of primitive types such as int and pointers)
#define HC_DATAMODEL_UNKNOWN 9999
#define HC_DATAMODEL_ILP32 1 // int, long and pointer are 32 bits (32 bit platforms)
#define HC_DATAMODEL_LLP64 2 // int and long are 32 bit; long long and pointer are 64 bits (64 bit Windows)
#define HC_DATAMODEL_LP64 3 // int is 32 bit; long and pointer are 64 bits (64 bit Unix)
// see http://www.unix.org/version2/whatsnew/lp64_wp.html for detailed descriptions

#if defined(HC_PLATFORM) 
#if !defined(HC_DATAMODEL)
    #error When setting HC_PLATFORM, also please specify the datamodel manually by setting the HC_DATAMODEL macro in your compiler.
#endif
#elif defined(_WIN32)
    #include <sdkddkver.h>
    #include <winapifamily.h>

    #if defined(_GAMING_DESKTOP) || defined(_GAMING_XBOX) || defined(_GAMING_XBOX_XBOXONE) || defined(_GAMING_XBOX_SCARLETT)
        #define HC_PLATFORM HC_PLATFORM_GDK
        
        #if !defined(NTDDI_WIN10_VB)
            #define APP_LOCAL_DEVICE_ID_SIZE 32
            typedef struct APP_LOCAL_DEVICE_ID
            {
                BYTE value[APP_LOCAL_DEVICE_ID_SIZE];
            } APP_LOCAL_DEVICE_ID;
        #endif
    #elif !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
        #define HC_PLATFORM HC_PLATFORM_WIN32
    #elif WINAPI_FAMILY == WINAPI_FAMILY_PC_APP && _WIN32_WINNT >= _WIN32_WINNT_WIN10
        #define HC_PLATFORM HC_PLATFORM_UWP
    #elif WINAPI_FAMILY == WINAPI_FAMILY_TV_APP || WINAPI_FAMILY == WINAPI_FAMILY_TV_TITLE
        #define HC_PLATFORM HC_PLATFORM_XDK
    #else
        #error Cannot recognize Windows flavor
    #endif

    #if defined(_WIN64)
        #define HC_DATAMODEL HC_DATAMODEL_LLP64
    #else
        #define HC_DATAMODEL HC_DATAMODEL_ILP32
    #endif
#elif defined(__linux__)
    #if defined(__ANDROID__)
        #define HC_PLATFORM HC_PLATFORM_ANDROID
    #else
        #define HC_PLATFORM HC_PLATFORM_LINUX
    #endif

    #if defined(__LP64__)
        #define HC_DATAMODEL HC_DATAMODEL_LP64
    #else
        #define HC_DATAMODEL HC_DATAMODEL_ILP32
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC == 1
        #if TARGET_OS_OSX
            #define HC_PLATFORM HC_PLATFORM_MAC
        #else
            #define HC_PLATFORM HC_PLATFORM_IOS
        #endif
    #else
        #error Cannot recognize Apple OS flavor
    #endif

    #if defined(__LP64__)
        #define HC_DATAMODEL HC_DATAMODEL_LP64
    #else
        #define HC_DATAMODEL HC_DATAMODEL_ILP32
    #endif
#else
    #if !defined(HC_DATAMODEL)
        #error libHttpClient does not recognize this platform, please specify the datamodel manually by setting the HC_DATAMODEL macro in your compiler.
    #endif
#endif

// HC_PLATFORM defines the "os" that libHttpClient is being built for
#if !defined(HC_PLATFORM)
#pragma warning(libHttpClient does not recognize this platform)
#define HC_PLATFORM HC_PLATFORM_UNKNOWN
#endif

// HC_PLATFORM defines the datamodel that libHttpClient is being built for
#if !defined(HC_DATAMODEL)
#error libHttpClient does not recognize the datamodel used on this platform.
#define HC_DATAMODEL HC_DATAMODEL_UNKNOWN
#elif HC_DATAMODEL != HC_DATAMODEL_ILP32 && HC_DATAMODEL != HC_DATAMODEL_LLP64 && HC_DATAMODEL != HC_DATAMODEL_LP64
#error HC_DATAMODEL is not set to a valid value, it must be one of HC_DATAMODEL_ILP32, HC_DATAMODEL_LLP64, or HC_DATAMODEL_LP64.
#endif

#if !defined(HC_PLATFORM_IS_MICROSOFT)
#define HC_PLATFORM_IS_MICROSOFT \
(HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK  || HC_PLATFORM == HC_PLATFORM_GDK)
#endif

#if !defined(HC_PLATFORM_IS_APPLE)
#define HC_PLATFORM_IS_APPLE \
(HC_PLATFORM == HC_PLATFORM_MAC || HC_PLATFORM == HC_PLATFORM_IOS)
#endif

// HC_PLATFORM_IS_EXTERNAL describes platforms where the implementation is outside of the libHttpClient repository
#if !defined(HC_PLATFORM_IS_EXTERNAL)
#define HC_PLATFORM_IS_EXTERNAL \
(HC_PLATFORM == HC_PLATFORM_NINTENDO_SWITCH || HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_4 || HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_5 || HC_PLATFORM == HC_PLATFORM_GENERIC)
#endif

#if !defined(HC_PLATFORM_IS_PLAYSTATION)
#define HC_PLATFORM_IS_PLAYSTATION \
(HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_4 || HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_5)
#endif

#if defined(HC_PLATFORM_MSBUILD_GUESS) && (HC_PLATFORM_MSBUILD_GUESS != HC_PLATFORM)
    #error The platform guessed by MSBuild does not agree with the platform selected by config.h
#endif

#if !defined(HC_WINHTTP_WEBSOCKETS)
#define HC_WINHTTP_WEBSOCKETS \
(HC_PLATFORM == HC_PLATFORM_GDK)
#endif
