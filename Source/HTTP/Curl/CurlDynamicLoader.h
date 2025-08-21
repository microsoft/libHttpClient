#pragma once

//
// This header is always includable across platforms. On non-GDK platforms,
// the macros are defined as direct calls and the dynamic loader class is absent.
// On GDK, the dynamic loader class is available and macros route through it.
//

#if HC_PLATFORM == HC_PLATFORM_GDK

#include <windows.h>
#include <memory>
#include <mutex>
#include <XCurl.h>

namespace xbox
{
namespace httpclient
{

// Dynamic curl function pointers
class CurlDynamicLoader
{
public:
    // Initialization/Cleanup functions
    using curl_global_init_ptr = CURLcode(*)(long flags);
    using curl_global_cleanup_ptr = void(*)();
    
    // Easy interface functions
    using curl_easy_init_ptr = CURL*(*)();
    using curl_easy_cleanup_ptr = void(*)(CURL* curl);
    using curl_easy_setopt_ptr = CURLcode(*)(CURL* curl, CURLoption option, ...);
    using curl_easy_getinfo_ptr = CURLcode(*)(CURL* curl, CURLINFO info, ...);
    using curl_easy_strerror_ptr = const char*(*)(CURLcode code);
    
    // String list functions
    using curl_slist_append_ptr = struct curl_slist*(*)(struct curl_slist* list, const char* string);
    using curl_slist_free_all_ptr = void(*)(struct curl_slist* list);
    
    // Multi interface functions
    using curl_multi_init_ptr = CURLM*(*)();
    using curl_multi_cleanup_ptr = CURLMcode(*)(CURLM* multi_handle);
    using curl_multi_add_handle_ptr = CURLMcode(*)(CURLM* multi_handle, CURL* curl_handle);
    using curl_multi_remove_handle_ptr = CURLMcode(*)(CURLM* multi_handle, CURL* curl_handle);
    using curl_multi_perform_ptr = CURLMcode(*)(CURLM* multi_handle, int* running_handles);
    using curl_multi_info_read_ptr = CURLMsg*(*)(CURLM* multi_handle, int* msgs_in_queue);
    using curl_multi_poll_ptr = CURLMcode(*)(CURLM* multi_handle, struct curl_waitfd extra_fds[], unsigned int extra_nfds, int timeout_ms, int* ret);
    using curl_multi_wait_ptr = CURLMcode(*)(CURLM* multi_handle, struct curl_waitfd extra_fds[], unsigned int extra_nfds, int timeout_ms, int* numfds);

    // Function pointers
    curl_global_init_ptr curl_global_init_fn = nullptr;
    curl_global_cleanup_ptr curl_global_cleanup_fn = nullptr;
    curl_easy_init_ptr curl_easy_init_fn = nullptr;
    curl_easy_cleanup_ptr curl_easy_cleanup_fn = nullptr;
    curl_easy_setopt_ptr curl_easy_setopt_fn = nullptr;
    curl_easy_getinfo_ptr curl_easy_getinfo_fn = nullptr;
    curl_easy_strerror_ptr curl_easy_strerror_fn = nullptr;
    curl_slist_append_ptr curl_slist_append_fn = nullptr;
    curl_slist_free_all_ptr curl_slist_free_all_fn = nullptr;
    curl_multi_init_ptr curl_multi_init_fn = nullptr;
    curl_multi_cleanup_ptr curl_multi_cleanup_fn = nullptr;
    curl_multi_add_handle_ptr curl_multi_add_handle_fn = nullptr;
    curl_multi_remove_handle_ptr curl_multi_remove_handle_fn = nullptr;
    curl_multi_perform_ptr curl_multi_perform_fn = nullptr;
    curl_multi_info_read_ptr curl_multi_info_read_fn = nullptr;
    curl_multi_poll_ptr curl_multi_poll_fn = nullptr;
    curl_multi_wait_ptr curl_multi_wait_fn = nullptr;

    static CurlDynamicLoader& GetInstance();
    // Frees the singleton instance and unloads XCurl.dll (via destructor -> Cleanup)
    static void DestroyInstance();
    ~CurlDynamicLoader();
    
    bool Initialize();
    void Cleanup();
    bool IsLoaded() const { return m_curlLibrary != nullptr; }

private:
    CurlDynamicLoader() = default;
    
    bool LoadFunction(FARPROC& funcPtr, const char* functionName);
    
    HMODULE m_curlLibrary = nullptr;
    
    // Thread safety
    static std::mutex s_initMutex;
    static HC_UNIQUE_PTR<CurlDynamicLoader> s_instance;
};

} // httpclient
} // xbox

// GDK macro variants: route through dynamic loader and provide default returns when not loaded
#define CURL_CALL(func_name) ::xbox::httpclient::CurlDynamicLoader::GetInstance().func_name##_fn
#define CURL_INVOKE_OR(defaultRet, func, ...) \
    ((::xbox::httpclient::CurlDynamicLoader::GetInstance().IsLoaded()) ? \
        (::xbox::httpclient::CurlDynamicLoader::GetInstance().func##_fn(__VA_ARGS__)) : \
        (defaultRet))
// Convenience when defaultRet == 0 (common for void-calls or zero-initialized return types)
#define CURL_INVOKE(func, ...) CURL_INVOKE_OR(0, func, __VA_ARGS__)

#else // non-GDK

// Non-GDK macro variants: call directly
#define CURL_CALL(func_name) func_name
#define CURL_INVOKE_OR(defaultRet, func, ...) func(__VA_ARGS__)
// Convenience when defaultRet == 0
#define CURL_INVOKE(func, ...) func(__VA_ARGS__)

#endif // HC_PLATFORM == HC_PLATFORM_GDK
