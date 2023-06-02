#pragma once

#include "StdOptional.h"
#include "Types.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Generic result class. Holds HRESULT, optional error message, and result payload
template<typename T>
class Result
{
public:
    // Construct successful result
    Result(T&& payload);

    // Failed result (no payload)
    Result(HRESULT hr);
    Result(HRESULT hr, String&& errorMessage);
    Result(HRESULT hr, const String& errorMessage);

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;
    ~Result() = default;

    HRESULT const hr;
    String const errorMessage;

    // Get result payload. Asserts if there is no payload
    const T& Payload() const;
    T&& ExtractPayload();

private:
    StdExtra::optional<T> m_payload{};
};

// Result specialization without a payload
template<>
class Result<void>
{
public:
    // Construct successful result
    Result();

    // Failed result (no payload)
    Result(HRESULT hr);
    Result(HRESULT hr, String&& errorMessage);
    Result(HRESULT hr, const String& errorMessage);

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;
    ~Result() = default;

    HRESULT const hr;
    String const errorMessage;
};

template<typename T>
bool Succeeded(const Result<T>& result)
{
    return SUCCEEDED(result.hr);
}

template<typename T>
bool Failed(const Result<T>& result)
{
    return FAILED(result.hr);
}

//------------------------------------------------------------------------------
// Template implementations
//------------------------------------------------------------------------------

template<typename T>
Result<T>::Result(T&& payload) :
    hr{ S_OK },
    m_payload{ std::move(payload) }
{
}

template<typename T>
Result<T>::Result(HRESULT hr_) :
    hr{ hr_ }
{
    assert(FAILED(hr));
}

template<typename T>
Result<T>::Result(HRESULT hr_, String&& errorMessage_) :
    hr{ hr_ },
    errorMessage{ std::move(errorMessage_) }
{
    assert(FAILED(hr));
}

template<typename T>
Result<T>::Result(HRESULT hr_, const String& errorMessage_) :
    hr{ hr_ },
    errorMessage{ errorMessage_ }
{
    assert(FAILED(hr));
}

template<typename T>
const T& Result<T>::Payload() const
{
    assert(m_payload.has_value());
    return *m_payload;
}

template<typename T>
T&& Result<T>::ExtractPayload()
{
    assert(m_payload.has_value());
    return std::move(*m_payload);
}

inline Result<void>::Result() :
    hr{ S_OK }
{
}

inline Result<void>::Result(HRESULT hr_) :
    hr{ hr_ }
{
}

inline Result<void>::Result(HRESULT hr_, String&& errorMessage_) :
    hr{ hr_ },
    errorMessage{ std::move(errorMessage_) }
{
    assert(FAILED(hr));
}

inline Result<void>::Result(HRESULT hr_, const String& errorMessage_) :
    hr{ hr_ },
    errorMessage{ errorMessage_ }
{
    assert(FAILED(hr));
}

NAMESPACE_XBOX_HTTP_CLIENT_END