// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include <new>
#include <stddef.h>
#include <sstream>
#include <set>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class http_memory
{
public:
    static _Ret_maybenull_ _Post_writable_byte_size_(size) void* mem_alloc(
        _In_ size_t size
        );

    static void mem_free(
        _In_opt_ void* pAddress
        );

    http_memory() = delete;
    http_memory(const http_memory&) = delete;
    http_memory& operator=(const http_memory&) = delete;
};


template<typename T, class... TArgs>
inline T* Make(TArgs&&... args)
{
    auto mem = http_memory::mem_alloc(sizeof(T));
    return new (mem) T(std::forward<TArgs>(args)...);
}

template<typename T>
inline void Delete(T* ptr)
{
    if (ptr != nullptr)
    {
        ptr->~T();
        http_memory::mem_free((void*)ptr);
    }
}

class http_memory_buffer
{
public:
    http_memory_buffer(_In_ size_t dwSize)
    {
        m_pBuffer = http_memory::mem_alloc(dwSize);
    }

    ~http_memory_buffer()
    {
        http_memory::mem_free(m_pBuffer);
        m_pBuffer = nullptr;
    }

    void* get()
    {
        return m_pBuffer;
    }

private:
    void* m_pBuffer;
};

NAMESPACE_XBOX_HTTP_CLIENT_END

template<typename T>
class http_stl_allocator
{
public:
    typedef T value_type;

    http_stl_allocator() = default;
    template<class U> http_stl_allocator(http_stl_allocator<U> const&) {}

    T* allocate(size_t n)
    {
        T* p = static_cast<T*>(xbox::httpclient::http_memory::mem_alloc(n * sizeof(T)));
        if (p == nullptr)
        {
            throw std::bad_alloc();
        }
        return p;
    }

    void deallocate(_In_opt_ void* p, size_t)
    {
        xbox::httpclient::http_memory::mem_free(p);
    }
};

template<typename T>
struct http_alloc_deleter
{
    http_alloc_deleter() {}
    http_alloc_deleter(const http_stl_allocator<T>& alloc) : m_alloc(alloc) { }

    void operator()(typename std::allocator_traits<http_stl_allocator<T>>::pointer p) const
    {
        http_stl_allocator<T> alloc(m_alloc);
        std::allocator_traits<http_stl_allocator<T>>::destroy(alloc, std::addressof(*p));
        std::allocator_traits<http_stl_allocator<T>>::deallocate(alloc, p, 1);
    }

private:
    http_stl_allocator<T> m_alloc;
};

template<typename T, typename... Args>
std::shared_ptr<T> http_allocate_shared(Args&&... args)
{
    return std::allocate_shared<T, http_stl_allocator<T>>(http_stl_allocator<T>(), std::forward<Args>(args)...);
}

template<typename T, typename... Args>
std::unique_ptr<T, http_alloc_deleter<T>> http_allocate_unique(Args&&... args)
{
    http_stl_allocator<T> alloc;
    auto p = std::allocator_traits<http_stl_allocator<T>>::allocate(alloc, 1); // malloc memory
    auto o = new(p) T(std::forward<Args>(args)...); // call class ctor using placement new
    return std::unique_ptr<T, http_alloc_deleter<T>>(o, http_alloc_deleter<T>(alloc));
}

template<typename T>
using HC_UNIQUE_PTR = std::unique_ptr<T, http_alloc_deleter<T>>;

template<typename T1, typename T2>
inline bool operator==(const http_stl_allocator<T1>&, const http_stl_allocator<T2>&)
{
    return true;
}

template<typename T1, typename T2>
bool operator!=(const http_stl_allocator<T1>&, const http_stl_allocator<T2>&)
{
    return false;
}

template<class T>
using http_internal_vector = std::vector<T, http_stl_allocator<T>>;

template<class K, class V, class LESS = std::less<K>>
using http_internal_map = std::map<K, V, LESS, http_stl_allocator<std::pair<K const, V>>>;

template<class K, class V, class HASH = std::hash<K>, class EQUAL = std::equal_to<K>>
using http_internal_unordered_map = std::unordered_map<K, V, HASH, EQUAL, http_stl_allocator<std::pair<K const, V>>>;

template<class C, class TRAITS = std::char_traits<C>>
using http_internal_basic_string = std::basic_string<C, TRAITS, http_stl_allocator<C>>;

using http_internal_string = http_internal_basic_string<char>;
using http_internal_wstring = http_internal_basic_string<wchar_t>;

template<class C, class TRAITS = std::char_traits<C>>
using http_internal_basic_stringstream = std::basic_stringstream<C, TRAITS, http_stl_allocator<C>>;

using http_internal_stringstream = http_internal_basic_stringstream<char>;

template<class T>
using http_internal_dequeue = std::deque<T, http_stl_allocator<T>>;

template<class T>
using http_internal_queue = std::queue<T, http_internal_dequeue<T>>;

template<class T>
using http_internal_list = std::list<T, http_stl_allocator<T>>;

template<class T, class LESS = std::less<T>>
using http_internal_set = std::set<T, LESS, http_stl_allocator<T>>;
