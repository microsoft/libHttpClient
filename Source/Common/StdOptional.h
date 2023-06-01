// Copied from the Visual Studio 2017 Update 3 optional header

// optional standard header
// Copyright (c) Microsoft Corporation. All rights reserved.
#pragma once

#if _HAS_CXX17 || _HAS_CPP17 || !defined(_WIN32)
// use std implementation if available
#include <optional>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN
namespace StdExtra = std;
NAMESPACE_XBOX_HTTP_CLIENT_END

#else

#include <exception>
#include <initializer_list>
#include <new>
#include <type_traits>

#pragma pack(push,8) // see _CRT_PACKING in vadefs.h
#pragma push_macro("new")
#undef new

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace StdExtra
{

//------------------------------------------------------------------------------
// Begin copied chunks
//------------------------------------------------------------------------------

// copied from Visual Studio 2017 Update 3 xmemory0 header
// TEMPLATE FUNCTION _Construct_in_place
template<class _Ty,
    class... _Types> inline
    void _Construct_in_place_p(_Ty& _Obj, _Types&&... _Args)
    noexcept(std::is_nothrow_constructible<_Ty, _Types...>::value)
{   // invoke True Placement New to initialize the referenced object with _Args...
    ::new (const_cast<void *>(static_cast<const volatile void *>(std::addressof(_Obj))))
        _Ty(std::forward<_Types>(_Args)...);
}

// TEMPLATE FUNCTION _Destroy_in_place_p
template<class _Ty> inline
void _Destroy_in_place_p(_Ty& _Obj) noexcept
{   // destroy the referenced object
    _Obj.~_Ty();
}

// copied from Visual Studio 2017 Update 3 utility header
// in_place TAG TYPE TEMPLATES
struct in_place_t
{   // tag used to select a constructor which initializes a contained object in place
    explicit in_place_t() = default;
};
/* inline */ constexpr in_place_t in_place{};

// copied from Visual Studio 2017 Update 3 xsmf_control.h
// TYPE TRAIT _Is_trivially_swappable
namespace _Has_ADL_swap_detail {
    void swap(); // undefined (deliberate shadowing)

    struct _Has_ADL_swap_unique_type
    {	// TRANSITION, C1XX
    };

    template<class,
        class = void>
        struct _Has_ADL_swap
        : std::false_type
    {};
    template<class _Ty>
    struct _Has_ADL_swap<_Ty,
        std::void_t<_Has_ADL_swap_unique_type,
        decltype(swap(std::declval<_Ty&>(), std::declval<_Ty&>()))>>
        : std::true_type
    {};
} // namespace _Has_ADL_swap_detail
using _Has_ADL_swap_detail::_Has_ADL_swap;

template<class _Ty>
struct _Is_trivially_swappable
    : std::conjunction<
    std::is_trivially_destructible<_Ty>,
    std::is_trivially_move_constructible<_Ty>,
    std::is_trivially_move_assignable<_Ty>,
    std::negation<_Has_ADL_swap<_Ty>>>::type
{   // true_type if it is valid to swap two _Ty lvalues by exchanging
    // object representations.
};

template<class _Ty>
struct is_nothrow_swappable
    : std::_Is_nothrow_swappable<_Ty>::type
{   // Determine if _Ty lvalues satisfy is_nothrow_swappable_with
};

template<class _Ty>
struct is_swappable
    : std::_Is_swappable<_Ty>::type
{   // Determine if _Ty lvalues satisfy is_swappable_with
};

//------------------------------------------------------------------------------
// End copied chunks
//------------------------------------------------------------------------------

// STRUCT nullopt_t [optional.nullopt]
struct nullopt_t
{   // no-value state indicator
    struct _Tag {};
    constexpr explicit nullopt_t(_Tag) {}
};
/* inline */ constexpr nullopt_t nullopt{ nullopt_t::_Tag{} };

// CLASS bad_optional_access [optional.bad_optional_access]
class bad_optional_access
    : public std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return ("Bad optional access");
    }
};

template<class _Ty,
    bool = std::is_trivially_destructible<_Ty>::value>
    struct _Optional_destruct_base
{   // either contains a value of _Ty or is empty (trivial destructor)
    union
    {
        char _Dummy;
        std::remove_cv_t<_Ty> _Value;
    };
    bool _Has_value;

    constexpr _Optional_destruct_base() noexcept
        : _Dummy{}, _Has_value{ false }
    {   // initialize an empty optional
    }

    template<class... _Types>
    constexpr explicit _Optional_destruct_base(in_place_t, _Types&&... _Args)
        : _Value(std::forward<_Types>(_Args)...), _Has_value{ true }
    {   // initialize contained value with _Args...
    }

#if 1 // TRANSITION, VSO#255357
    _Optional_destruct_base(const _Optional_destruct_base&) = default;
    _Optional_destruct_base(_Optional_destruct_base&&) = default;
    _Optional_destruct_base& operator=(const _Optional_destruct_base&) = default;
    _Optional_destruct_base& operator=(_Optional_destruct_base&&) = default;
#endif // TRANSITION, VSO#255357

    void reset() noexcept
    {   // destroy any contained value and transition to the empty state
        this->_Has_value = false;
    }
};

template<class _Ty>
struct _Optional_destruct_base<_Ty, false>
{   // either contains a value of _Ty or is empty (non-trivial destructor)
    union
    {
        char _Dummy;
        std::remove_cv_t<_Ty> _Value;
    };
    bool _Has_value;

    ~_Optional_destruct_base() noexcept
    {   // destroy any contained value
        if (_Has_value)
        {
            StdExtra::_Destroy_in_place_p(_Value);
        }
    }

    constexpr _Optional_destruct_base() noexcept
        : _Dummy{}, _Has_value{ false }
    {   // initialize an empty optional
    }

    template<class... _Types>
    constexpr explicit _Optional_destruct_base(in_place_t, _Types&&... _Args)
        : _Value(std::forward<_Types>(_Args)...), _Has_value{ true }
    {   // initialize contained value with _Args...
    }

    _Optional_destruct_base(const _Optional_destruct_base&) = default;
    _Optional_destruct_base(_Optional_destruct_base&&) = default;
    _Optional_destruct_base& operator=(const _Optional_destruct_base&) = default;
    _Optional_destruct_base& operator=(_Optional_destruct_base&&) = default;

    void reset() noexcept
    {   // destroy any contained value and transition to the empty state
        if (this->_Has_value)
        {
            StdExtra::_Destroy_in_place_p(this->_Value);
            this->_Has_value = false;
        }
    }
};

template<class _Ty>
struct _Optional_construct_base
    : _Optional_destruct_base<_Ty>
{   // Layer common behaviors atop the _Optional_destruct_base implementations
    using _Optional_destruct_base<_Ty>::_Optional_destruct_base;

    template<class... _Types>
    _Ty& _Construct(_Types&&... _Args)
    {   // transition from the empty to the value-containing state
        // Pre: !this->_Has_value
        StdExtra::_Construct_in_place_p(this->_Value, std::forward<_Types>(_Args)...);
        this->_Has_value = true;
        return (this->_Value);
    }

    template<class _Ty2>
    void _Assign(_Ty2&& _Right)
    {   // assign / initialize the contained value from _Right
        if (this->_Has_value)
        {
            this->_Value = std::forward<_Ty2>(_Right);
        }
        else
        {
            _Construct(std::forward<_Ty2>(_Right));
        }
    }

    template<class _Self>
    void _Construct_from(_Self&& _Right)
        noexcept(std::is_nothrow_constructible<_Ty, decltype((std::forward<_Self>(_Right)._Value))>::value)
    {   // initialize contained value from _Right iff it contains a value
        if (_Right._Has_value)
        {
            _Construct(std::forward<_Self>(_Right)._Value);
        }
    }

    template<class _Self>
    void _Assign_from(_Self&& _Right)
        noexcept(std::is_nothrow_constructible<_Ty, decltype((std::forward<_Self>(_Right)._Value))>::value
            && std::is_nothrow_assignable<_Ty&, decltype((std::forward<_Self>(_Right)._Value))>::value)
    {   // assign/initialize/destroy contained value from _Right
        if (_Right._Has_value)
        {
            _Assign(std::forward<_Self>(_Right)._Value);
        }
        else
        {
            this->reset();
        }
    }

    _Ty& _Get() & noexcept
    {
        return (this->_Value);
    }
    const _Ty& _Get() const & noexcept
    {
        return (this->_Value);
    }
};

// CLASS TEMPLATE optional [optional.object]
template<class _Ty>
class optional
    : private _Optional_construct_base<_Ty>
{   // optional for object types
private:
    using _Mybase = _Optional_construct_base<_Ty>;
public:
    static_assert(!std::is_reference<_Ty>::value,
        "T in optional<T> cannot be a reference type (N4659 23.6.2 [optional.syn]/1).");
    static_assert(!std::is_same<std::remove_cv_t<_Ty>, nullopt_t>::value,
        "T in optional<T> cannot be nullopt_t (N4659 23.6.2 [optional.syn]/1).");
    static_assert(!std::is_same<std::remove_cv_t<_Ty>, in_place_t>::value,
        "T in optional<T> cannot be in_place_t (N4659 23.6.2 [optional.syn]/1).");
    static_assert(std::is_reference<_Ty>::value || std::is_object<_Ty>::value,
        "T in optional<T> must be an object type (N4659 23.6.3 [optional.optional]/3).");
    static_assert(std::is_destructible<_Ty>::value && !std::is_array<_Ty>::value,
        "T in optional<T> must satisfy the requirements of Destructible (N4659 23.6.3 [optional.optional]/3).");

    using value_type = _Ty;

    // constructors [optional.object.ctor]
    constexpr optional() noexcept
        : _Mybase{}
    {   // initialize to empty state
    }
    constexpr optional(nullopt_t) noexcept
        : _Mybase{}
    {   // initialize to empty state
    }

    template<class... _Types,
        class = std::enable_if_t<std::is_constructible<_Ty, _Types...>::value>>
        constexpr explicit optional(in_place_t, _Types&&... _Args)
        : _Mybase(in_place, std::forward<_Types>(_Args)...)
    {   // initialize contained value from _Args...
    }

    template<class _Elem,
        class... _Types,
        class = std::enable_if_t<std::is_constructible<_Ty, std::initializer_list<_Elem>&, _Types...>::value>>
        constexpr explicit optional(in_place_t, std::initializer_list<_Elem> _Ilist, _Types&&... _Args)
        : _Mybase(in_place, _Ilist, std::forward<_Types>(_Args)...)
    {   // initialize contained value from _Ilist and _Args...
    }

    template<class _Ty2>
    using _AllowDirectConversion = std::bool_constant<
        std::is_constructible<_Ty, _Ty2>::value
        && !std::is_same<std::decay_t<_Ty2>, in_place_t>::value
        && !std::is_same<std::decay_t<_Ty2>, optional>::value>;
    template<class _Ty2 = _Ty,
        std::enable_if_t<std::conjunction<_AllowDirectConversion<_Ty2>, std::is_convertible<_Ty2, _Ty>>::value, int> = 0>
        constexpr optional(_Ty2&& _Right)
        : _Mybase(in_place, std::forward<_Ty2>(_Right))
    {   // initialize contained value from _Right
    }
    template<class _Ty2 = _Ty,
        std::enable_if_t<std::conjunction<_AllowDirectConversion<_Ty2>, std::negation<std::is_convertible<_Ty2, _Ty>>>::value, int> = 0>
        constexpr explicit optional(_Ty2&& _Right)
        : _Mybase(in_place, std::forward<_Ty2>(_Right))
    {   // initialize contained value from _Right
    }

    template<class _Ty2>
    struct _AllowUnwrapping
        : std::bool_constant<
        !(std::is_constructible_v<_Ty, optional<_Ty2>&>
            || std::is_constructible_v<_Ty, const optional<_Ty2>&>
            || std::is_constructible_v<_Ty, const optional<_Ty2>>
            || std::is_constructible_v<_Ty, optional<_Ty2>>
            || std::is_convertible_v<optional<_Ty2>&, _Ty>
            || std::is_convertible_v<const optional<_Ty2>&, _Ty>
            || std::is_convertible_v<const optional<_Ty2>, _Ty>
            || std::is_convertible_v<optional<_Ty2>, _Ty>)>
    {};

    template<class _Ty2,
        std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, const _Ty2&>,
        _AllowUnwrapping<_Ty2>,
        std::is_convertible<const _Ty2&, _Ty>>::value, int> = 0>
        optional(const optional<_Ty2>& _Right)
    {   // possibly initialize contained value from _Right
        if (_Right)
        {
            this->_Construct(*_Right);
        }
    }
    template<class _Ty2,
        std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, const _Ty2&>,
        _AllowUnwrapping<_Ty2>,
        std::negation<std::is_convertible<const _Ty2&, _Ty>>>::value, int> = 0>
        explicit optional(const optional<_Ty2>& _Right)
    {   // possibly initialize contained value from _Right
        if (_Right)
        {
            this->_Construct(*_Right);
        }
    }

    optional(optional const& _Right)
    {   // possibly initialize contained value from _Right
        if (_Right)
        {
            this->_Construct(*_Right);
        }
    }

    template<class _Ty2,
        std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, _Ty2>,
        _AllowUnwrapping<_Ty2>,
        std::is_convertible<_Ty2, _Ty>>::value, int> = 0>
        optional(optional<_Ty2>&& _Right)
    {   // possibly initialize contained value from _Right
        if (_Right)
        {
            this->_Construct(std::move(*_Right));
        }
    }
    template<class _Ty2,
        std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, _Ty2>,
        _AllowUnwrapping<_Ty2>,
        std::negation<std::is_convertible<_Ty2, _Ty>>>::value, int> = 0>
        explicit optional(optional<_Ty2>&& _Right)
    {   // possibly initialize contained value from _Right
        if (_Right)
        {
            this->_Construct(std::move(*_Right));
        }
    }

    // assignment [optional.object.assign]
    optional& operator=(nullopt_t) noexcept
    {   // destroy any contained value and transition to empty state
        reset();
        return (*this);
    }

    template<class _Ty2 = _Ty,
        class = std::enable_if_t<std::conjunction<
        std::bool_constant<
        !std::is_same<optional, std::decay_t<_Ty2>>::value
        && !(std::is_scalar<_Ty>::value && std::is_same<_Ty, std::decay_t<_Ty2>>::value)>,
        std::is_constructible<_Ty, _Ty2>,
        std::is_assignable<_Ty&, _Ty2>>::value>>
        optional& operator=(_Ty2&& _Right)
    {   // assign/initialize contained value from _Right
        this->_Assign(std::forward<_Ty2>(_Right));
        return (*this);
    }

    template<class _Ty2>
    struct _AllowUnwrappingAssignment
        : std::bool_constant<
        !(std::is_assignable_v<_Ty&, optional<_Ty2>&>
            || std::is_assignable_v<_Ty&, const optional<_Ty2>&>
            || std::is_assignable_v<_Ty&, const optional<_Ty2>>
            || std::is_assignable_v<_Ty&, optional<_Ty2>>)>
    {};

    template<class _Ty2,
        class = std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, const _Ty2&>,
        std::is_assignable<_Ty&, const _Ty2&>,
        _AllowUnwrappingAssignment<_Ty2>>::value>>
        optional& operator=(const optional<_Ty2>& _Right)
    {   // assign/initialize/destroy contained value from _Right
        if (_Right)
        {
            this->_Assign(*_Right);
        }
        else
        {
            reset();
        }

        return (*this);
    }

    optional& operator=(optional const& _Right)
    {   // assign/initialize/destroy contained value from _Right
        if (_Right)
        {
            this->_Assign(*_Right);
        }
        else
        {
            reset();
        }

        return (*this);
    }

    template<class _Ty2,
        class = std::enable_if_t<std::conjunction<
        std::is_constructible<_Ty, _Ty2>,
        std::is_assignable<_Ty&, _Ty2>,
        _AllowUnwrappingAssignment<_Ty2>>::value>>
        optional& operator=(optional<_Ty2>&& _Right)
    {   // assign/initialize/destroy contained value from _Right
        if (_Right)
        {
            this->_Assign(std::move(*_Right));
        }
        else
        {
            reset();
        }

        return (*this);
    }

    template<class... _Types>
    _Ty& emplace(_Types&&... _Args)
    {   // destroy any contained value, then initialize from _Args...
        reset();
        return (this->_Construct(std::forward<_Types>(_Args)...));
    }

    template<class _Elem,
        class... _Types,
        class = std::enable_if_t<std::is_constructible<
        _Ty, std::initializer_list<_Elem>&, _Types...>::value>>
        _Ty& emplace(std::initializer_list<_Elem> _Ilist, _Types&&... _Args)
    {   // destroy any contained value, then initialize from _Ilist and _Args...
        reset();
        return (this->_Construct(_Ilist, std::forward<_Types>(_Args)...));
    }

    // swap [optional.object.swap]
    void swap(optional& _Right)
        noexcept(std::is_nothrow_move_constructible<_Ty>::value && is_nothrow_swappable<_Ty>::value)
    {   // exchange state with _Right
        static_assert(std::is_move_constructible<_Ty>::value,
            "optional<T>::swap requires T to be move constructible (N4659 23.6.3.4 [optional.swap]/1).");
        static_assert(!std::is_move_constructible<_Ty>::value || is_swappable<_Ty>::value,
            "optional<T>::swap requires T to be swappable (N4659 23.6.3.4 [optional.swap]/1).");
        _Swap(_Right, _Is_trivially_swappable<_Ty>{});
    }

    // observers [optional.object.observe]
    const _Ty * operator->() const
    {   // return pointer to contained value
        return (std::addressof(this->_Get()));
    }
    _Ty * operator->()
    {   // return pointer to contained value
        return (std::addressof(this->_Get()));
    }

    const _Ty& operator*() const &
    {   // return reference to contained value
        return (this->_Get());
    }
    _Ty& operator*() &
    {   // return reference to contained value
        return (this->_Get());
    }
    _Ty&& operator*() &&
    {   // return reference to contained value
        return (std::move(this->_Get()));
    }
    const _Ty&& operator*() const &&
    {   // return reference to contained value
        return (std::move(this->_Get()));
    }

    explicit operator bool() const noexcept
    {   // return true iff *this contains a value
        return (this->_Has_value);
    }
    bool has_value() const noexcept
    {   // return true iff *this contains a value
        return (this->_Has_value);
    }

    const _Ty& value() const &
    {   // return reference to contained value or throw if none
        if (!has_value())
        {
            throw bad_optional_access{};
        }

        return (this->_Get());
    }
    _Ty& value() &
    {   // return reference to contained value or throw if none
        if (!has_value())
        {
            throw bad_optional_access{};
        }

        return (this->_Get());
    }
    _Ty&& value() &&
    {   // return reference to contained value or throw if none
        if (!has_value())
        {
            throw bad_optional_access{};
        }

        return (std::move(this->_Get()));
    }
    const _Ty&& value() const &&
    {   // return reference to contained value or throw if none
        if (!has_value())
        {
            throw bad_optional_access{};
        }

        return (std::move(this->_Get()));
    }

    template<class _Ty2>
    _Ty value_or(_Ty2&& _Right) const &
    {   // return contained value or _Right if none
        static_assert(std::is_copy_constructible<_Ty>::value,
            "The const overload of optional<T>::value_or requires T to be copy constructible "
            "(N4659 23.6.3.5 [optional.observe]/18).");
        static_assert(std::is_convertible<_Ty2, _Ty>::value,
            "optional<T>::value_or(U) requires U to be convertible to T (N4659 23.6.3.5 [optional.observe]/18).");

        if (has_value())
        {
            return (this->_Get());
        }

        return (static_cast<_Ty>(std::forward<_Ty2>(_Right)));
    }
    template<class _Ty2>
    _Ty value_or(_Ty2&& _Right) &&
    {   // return contained value or _Right if none
        static_assert(std::is_move_constructible<_Ty>::value,
            "The rvalue overload of optional<T>::value_or requires T to be move constructible "
            "(N4659 23.6.3.5 [optional.observe]/20).");
        static_assert(std::is_convertible<_Ty2, _Ty>::value,
            "optional<T>::value_or(U) requires U to be convertible to T (N4659 23.6.3.5 [optional.observe]/20).");

        if (has_value())
        {
            return (std::move(this->_Get()));
        }

        return (static_cast<_Ty>(std::forward<_Ty2>(_Right)));
    }

    // modifiers [optional.object.mod]
    using _Mybase::reset;

private:
    void _Swap(optional& _Right, std::true_type)
    {   // implement trivial swap
        using _TrivialBaseTy = _Optional_destruct_base<_Ty>;
        std::swap(static_cast<_TrivialBaseTy&>(*this), static_cast<_TrivialBaseTy&>(_Right));
    }

    void _Swap(optional& _Right, std::false_type)
    {   // implement non-trivial swap
        const bool _Engaged = has_value();
        if (_Engaged == _Right.has_value())
        {
            if (_Engaged)
            {
                std::_Swap_adl(**this, *_Right);
            }
        }
        else
        {
            optional& _Source = _Engaged ? *this : _Right;
            optional& _Target = _Engaged ? _Right : *this;
            _Target._Construct(std::move(*_Source));
            _Source.reset();
        }
    }
};

// RELATIONAL OPERATORS [optional.relops]
template<class _Ty1,
    class _Ty2>
    bool operator==(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    const bool _Left_has_value = _Left.has_value();
    return (_Left_has_value == _Right.has_value() && (!_Left_has_value || *_Left == *_Right));
}

template<class _Ty1,
    class _Ty2>
    bool operator!=(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    const bool _Left_has_value = _Left.has_value();
    return (_Left_has_value != _Right.has_value() || (_Left_has_value && *_Left != *_Right));
}

template<class _Ty1,
    class _Ty2>
    constexpr bool operator<(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    return (_Right.has_value() && (!_Left.has_value() || *_Left < *_Right));
}

template<class _Ty1,
    class _Ty2>
    constexpr bool operator>(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    return (_Left.has_value() && (!_Right.has_value() || *_Left > *_Right));
}

template<class _Ty1,
    class _Ty2>
    constexpr bool operator<=(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    return (!_Left.has_value() || (_Right.has_value() && *_Left <= *_Right));
}

template<class _Ty1,
    class _Ty2>
    constexpr bool operator>=(const optional<_Ty1>& _Left, const optional<_Ty2>& _Right)
{
    return (!_Right.has_value() || (_Left.has_value() && *_Left >= *_Right));
}

// COMPARISONS WITH nullopt [optional.nullops]
template<class _Ty>
constexpr bool operator==(const optional<_Ty>& _Left, nullopt_t) noexcept
{
    return (!_Left.has_value());
}
template<class _Ty>
constexpr bool operator==(nullopt_t, const optional<_Ty>& _Right) noexcept
{
    return (!_Right.has_value());
}

template<class _Ty>
constexpr bool operator!=(const optional<_Ty>& _Left, nullopt_t) noexcept
{
    return (_Left.has_value());
}
template<class _Ty>
constexpr bool operator!=(nullopt_t, const optional<_Ty>& _Right) noexcept
{
    return (_Right.has_value());
}

template<class _Ty>
constexpr bool operator<(const optional<_Ty>&, nullopt_t) noexcept
{
    return (false);
}
template<class _Ty>
constexpr bool operator<(nullopt_t, const optional<_Ty>& _Right) noexcept
{
    return (_Right.has_value());
}

template<class _Ty>
constexpr bool operator<=(const optional<_Ty>& _Left, nullopt_t) noexcept
{
    return (!_Left.has_value());
}
template<class _Ty>
constexpr bool operator<=(nullopt_t, const optional<_Ty>&) noexcept
{
    return (true);
}

template<class _Ty>
constexpr bool operator>(const optional<_Ty>& _Left, nullopt_t) noexcept
{
    return (_Left.has_value());
}
template<class _Ty>
constexpr bool operator>(nullopt_t, const optional<_Ty>&) noexcept
{
    return (false);
}

template<class _Ty>
constexpr bool operator>=(const optional<_Ty>&, nullopt_t) noexcept
{
    return (true);
}
template<class _Ty>
constexpr bool operator>=(nullopt_t, const optional<_Ty>& _Right) noexcept
{
    return (!_Right.has_value());
}

// COMPARISONS WITH T [optional.comp_with_t]
struct _Unique_tag_are_comparable_with_equal
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_equal
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_equal<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_equal,
    decltype(std::declval<const _Lhs&>() == std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() == std::declval<const _Rhs&>()), bool>::type
{
};

struct _Unique_tag_are_comparable_with_not_equal
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_not_equal
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_not_equal<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_not_equal,
    decltype(std::declval<const _Lhs&>() != std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() != std::declval<const _Rhs&>()), bool>::type
{
};

struct _Unique_tag_are_comparable_with_less
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_less
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_less<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_less,
    decltype(std::declval<const _Lhs&>() < std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() < std::declval<const _Rhs&>()), bool>::type
{
};

struct _Unique_tag_are_comparable_with_less_equal
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_less_equal
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_less_equal<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_less_equal,
    decltype(std::declval<const _Lhs&>() <= std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() <= std::declval<const _Rhs&>()), bool>::type
{
};

struct _Unique_tag_are_comparable_with_greater
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_greater
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_greater<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_greater,
    decltype(std::declval<const _Lhs&>() > std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() > std::declval<const _Rhs&>()), bool>::type
{
};

struct _Unique_tag_are_comparable_with_greater_equal
{   // TRANSITION, C1XX
};

template<class _Lhs,
    class _Rhs,
    class = void>
    struct _Are_comparable_with_greater_equal
    : std::false_type
{
};

template<class _Lhs,
    class _Rhs>
    struct _Are_comparable_with_greater_equal<_Lhs, _Rhs, std::void_t<
    _Unique_tag_are_comparable_with_greater_equal,
    decltype(std::declval<const _Lhs&>() >= std::declval<const _Rhs&>())>>
    : std::is_convertible<decltype(std::declval<const _Lhs&>() >= std::declval<const _Rhs&>()), bool>::type
{
};

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_equal<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator==(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left == _Right : false);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_equal<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator==(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left == *_Right : false);
}

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_not_equal<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator!=(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left != _Right : true);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_not_equal<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator!=(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left != *_Right : true);
}

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_less<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator<(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left < _Right : true);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_less<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator<(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left < *_Right : false);
}

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_less_equal<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator<=(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left <= _Right : true);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_less_equal<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator<=(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left <= *_Right : false);
}

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_greater<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator>(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left > _Right : false);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_greater<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator>(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left > *_Right : true);
}

template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_greater_equal<_Ty1, _Ty2>::value, int> = 0>
    constexpr bool operator>=(const optional<_Ty1>& _Left, const _Ty2& _Right)
{
    return (_Left ? *_Left >= _Right : false);
}
template<class _Ty1,
    class _Ty2,
    std::enable_if_t<_Are_comparable_with_greater_equal<_Ty2, _Ty1>::value, int> = 0>
    constexpr bool operator>=(const _Ty2& _Left, const optional<_Ty1>& _Right)
{
    return (_Right ? _Left >= *_Right : true);
}

// FUNCTION TEMPLATE swap [optional.specalg]
template<class _Ty>
    inline void swap(optional<_Ty>& _Left, optional<_Ty>& _Right)
    noexcept(noexcept(_Left.swap(_Right)))
{   // exchange the values of _Left and _Right
    _Left.swap(_Right);
}

// FUNCTION TEMPLATE make_optional [optional.specalg]
template<class _Ty>
constexpr optional<std::decay_t<_Ty>> make_optional(_Ty&& _Value)
{   // Construct an optional from _Value
    return (optional<std::decay_t<_Ty>>{std::forward<_Ty>(_Value)});
}
template<class _Ty,
    class... _Types>
    constexpr optional<_Ty> make_optional(_Types&&... _Args)
{   // Construct an optional from _Args
    return (optional<_Ty>{in_place, std::forward<_Types>(_Args)...});
}
template<class _Ty,
    class _Elem,
    class... _Types>
    constexpr optional<_Ty> make_optional(std::initializer_list<_Elem> _Ilist, _Types&&... _Args)
{   // Construct an optional from _Ilist and _Args
    return (optional<_Ty>{in_place, _Ilist, std::forward<_Types>(_Args)...});
}

}

NAMESPACE_XBOX_HTTP_CLIENT_END

#pragma pop_macro("new")
#pragma pack(pop)

#endif
