#include "pch.h"
#include "CurlDynamicLoader.h"

#if HC_PLATFORM == HC_PLATFORM_GDK

#include <memory>
#include <mutex>

namespace xbox
{
namespace httpclient
{

std::mutex CurlDynamicLoader::s_initMutex;
HC_UNIQUE_PTR<CurlDynamicLoader> CurlDynamicLoader::s_instance;

CurlDynamicLoader& CurlDynamicLoader::GetInstance()
{
    std::lock_guard<std::mutex> lock(s_initMutex);
    if (!s_instance)
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "Creating CurlDynamicLoader instance");
        
        // Use libHttpClient custom allocator hooks while staying within class access to private ctor
        http_stl_allocator<CurlDynamicLoader> a{};
        s_instance = HC_UNIQUE_PTR<CurlDynamicLoader>{ new (a.allocate(1)) CurlDynamicLoader };
    }
    return *s_instance;
}

void CurlDynamicLoader::DestroyInstance()
{
    std::lock_guard<std::mutex> lock(s_initMutex);
    if (s_instance)
    {
        // Unique ptr with http_alloc_deleter ensures custom free hooks are used
        s_instance.reset();
    }
}

CurlDynamicLoader::~CurlDynamicLoader()
{
    Cleanup();
}

bool CurlDynamicLoader::Initialize()
{
    if (m_curlLibrary != nullptr)
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "XCurl.dll already loaded");
        return true; // Already loaded
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "Attempting to load XCurl.dll");
    
    // Try to load XCurl.dll
    m_curlLibrary = LoadLibraryA("XCurl.dll");
    if (m_curlLibrary == nullptr)
    {
        DWORD error = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "Failed to load XCurl.dll. Error code: %lu", error);
        return false;
    }

    // Load all required functions
    bool success = true;
    
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_global_init_fn), "curl_global_init");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_global_cleanup_fn), "curl_global_cleanup");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_easy_init_fn), "curl_easy_init");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_easy_cleanup_fn), "curl_easy_cleanup");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_easy_setopt_fn), "curl_easy_setopt");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_easy_getinfo_fn), "curl_easy_getinfo");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_easy_strerror_fn), "curl_easy_strerror");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_slist_append_fn), "curl_slist_append");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_slist_free_all_fn), "curl_slist_free_all");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_init_fn), "curl_multi_init");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_cleanup_fn), "curl_multi_cleanup");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_add_handle_fn), "curl_multi_add_handle");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_remove_handle_fn), "curl_multi_remove_handle");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_perform_fn), "curl_multi_perform");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_info_read_fn), "curl_multi_info_read");
    
    // Note: curl_multi_poll might not be available in older versions, so we make it optional
    LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_poll_fn), "curl_multi_poll");
    success &= LoadFunction(reinterpret_cast<FARPROC&>(curl_multi_wait_fn), "curl_multi_wait");

    if (!success)
    {
        Cleanup();
        return false;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "XCurl.dll loaded successfully");
    return true;
}

void CurlDynamicLoader::Cleanup()
{
    if (m_curlLibrary != nullptr)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "Unloading XCurl.dll");
        FreeLibrary(m_curlLibrary);
        m_curlLibrary = nullptr;
    }
    
    // Reset all function pointers
    curl_global_init_fn = nullptr;
    curl_global_cleanup_fn = nullptr;
    curl_easy_init_fn = nullptr;
    curl_easy_cleanup_fn = nullptr;
    curl_easy_setopt_fn = nullptr;
    curl_easy_getinfo_fn = nullptr;
    curl_easy_strerror_fn = nullptr;
    curl_slist_append_fn = nullptr;
    curl_slist_free_all_fn = nullptr;
    curl_multi_init_fn = nullptr;
    curl_multi_cleanup_fn = nullptr;
    curl_multi_add_handle_fn = nullptr;
    curl_multi_remove_handle_fn = nullptr;
    curl_multi_perform_fn = nullptr;
    curl_multi_info_read_fn = nullptr;
    curl_multi_poll_fn = nullptr;
    curl_multi_wait_fn = nullptr;
}

bool CurlDynamicLoader::LoadFunction(FARPROC& funcPtr, const char* functionName)
{
    funcPtr = GetProcAddress(m_curlLibrary, functionName);
    if (funcPtr == nullptr)
    {
        DWORD error = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "Failed to load function: %s. Error code: %lu", functionName, error);
        return false;
    }
    return true;
}

} // httpclient
} // xbox

#endif // HC_PLATFORM == HC_PLATFORM_GDK
