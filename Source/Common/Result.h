#pragma once

template<typename T>
class Result
{
public:
    // Construct successful result
    Result(T&& payload);

    // Failed result (no payload)
    Result(HRESULT hr);

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;
    ~Result() = default;

    HRESULT const hr;

    // Get result payload. Asserts if result isn't successful.
    const T& Payload() const;
    T&& ExtractPayload();

private:
    T m_payload{};
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

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;
    ~Result() = default;

    HRESULT hr;
};

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
}

template<typename T>
const T& Result<T>::Payload() const
{
    assert(SUCCEEDED(hr));
    return m_payload;
}

template<typename T>
T&& Result<T>::ExtractPayload()
{
    assert(SUCCEEDED(hr));
    return std::move(m_payload);
}

inline Result<void>::Result() :
    hr{ S_OK }
{
}

inline Result<void>::Result(HRESULT hr_) :
    hr{ hr_ }
{
}
