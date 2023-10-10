#pragma once

#include <list>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Global/mem.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Allocator types
template<typename T>
using Allocator = http_stl_allocator<T>;

template<typename T>
using Deleter = http_alloc_deleter<T>;

// Smart pointers
template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

template<typename T>
using UniquePtr = std::unique_ptr<T, Deleter<T>>;

// Containers
template<class T>
using Vector = std::vector<T, Allocator<T>>;

template<class K, class V, class LESS = std::less<K>>
using Map = std::map<K, V, LESS, Allocator<std::pair<K const, V>>>;

template<class K, class LESS = std::less<K>>
using Set = std::set<K, LESS, Allocator<K>>;

template<class K, class V, class HASH = std::hash<K>, class EQUAL = std::equal_to<K>>
using UnorderedMap = std::unordered_map<K, V, HASH, EQUAL, Allocator<std::pair<K const, V>>>;

template<class K, class HASH = std::hash<K>, class EQUAL = std::equal_to<K>>
using UnorderedSet = std::unordered_set<K, HASH, EQUAL, Allocator<K>>;

template<class C, class TRAITS = std::char_traits<C>>
using BasicString = std::basic_string<C, TRAITS, Allocator<C>>;
using String = BasicString<char>;
using WString = BasicString<wchar_t>;

template<class C, class TRAITS = std::char_traits<C>>
using BasicStringsteam = std::basic_stringstream<C, TRAITS, Allocator<C>>;
using Stringstream = BasicStringsteam<char>;
using WStringstream = BasicStringsteam<wchar_t>;

template<class T>
using Deque = std::deque<T, Allocator<T>>;

template<class T>
using Queue = std::queue<T, Deque<T>>;

template<class T>
using List = std::list<T, Allocator<T>>;

NAMESPACE_XBOX_HTTP_CLIENT_END