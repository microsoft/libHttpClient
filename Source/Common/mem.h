// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include <new>
#include <stddef.h>

NAMESPACE_XBOX_LIBHCBEGIN

class http_memory
{
public:
    static _Ret_maybenull_ _Post_writable_byte_size_(dwSize) void* mem_alloc(
        _In_ size_t dwSize
        );

    static void mem_free(
        _In_ void* pAddress
        );

private:
    http_memory();
    http_memory(const http_memory&);
    http_memory& operator=(const http_memory&);
};

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

NAMESPACE_XBOX_LIBHCEND

template<typename T>
class http_stl_allocator
{
public:
    http_stl_allocator() { }

    template<typename Other> http_stl_allocator(const http_stl_allocator<Other> &) { }

    template<typename Other>
    struct rebind
    {
        typedef http_stl_allocator<Other> other;
    };

    typedef size_t      size_type;
    typedef ptrdiff_t   difference_type;
    typedef T*          pointer;
    typedef const T*    const_pointer;
    typedef T&          reference;
    typedef const T&    const_reference;
    typedef T           value_type;

    pointer allocate(size_type n, const void * = 0)
    {
        pointer p = reinterpret_cast<pointer>(xbox::livehttpclient::http_memory::mem_alloc(n * sizeof(T)));

        if (p == NULL)
        {
            throw std::bad_alloc();
        }
        return p;
    }

    void deallocate(_In_opt_ void* p, size_type)
    {
        xbox::livehttpclient::http_memory::mem_free(p);
    }

    char* _Charalloc(size_type n)
    {
        char* p = reinterpret_cast<char*>(xbox::livehttpclient::http_memory::mem_alloc(n));

        if (p == NULL)
        {
            throw std::bad_alloc();
        }
        return p;
    }

    void construct(_In_ pointer p, const_reference t)
    {
        new ((void*)p) T(t);
    }

    void destroy(_In_ pointer p)
    {
        p; // Needed to avoid unreferenced param on VS2012
        p->~T();
    }

    size_t max_size() const
    {
        size_t n = (size_t)(-1) / sizeof(T);
        return (0 < n ? n : 1);
    }
};

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

#define http_internal_vector(T) std::vector<T, http_stl_allocator<T> >
#define http_internal_unordered_map(Key, T) std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>, http_stl_allocator< std::pair< const Key, T > > >
#define http_internal_string std::basic_string<char_t, std::char_traits<char_t>, http_stl_allocator<char_t> >
#define http_internal_dequeue(T) std::deque<T, http_stl_allocator<T> >
#define http_internal_map(T1, T2) std::map<T1, T2, std::less<T1>, http_stl_allocator<std::pair<T1,T2>> >
#define http_internal_queue(T) std::queue<T,std::deque<T, http_stl_allocator<T>>>

