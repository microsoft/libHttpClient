// Copyright(c) Microsoft Corporation. All rights reserved.

#pragma once

template <class T, class R>
class CallbackThunk
{
public:
    CallbackThunk(std::function<R(T)> func)
        : _func(func)
    {
    }

    static R Callback(void * context, T data)
    {
        const CallbackThunk<T, R>* pthis = static_cast<CallbackThunk<T, R>*>(context);
        return pthis->_func(data);
    }

private:

    std::function<R(T)> _func;
};

template <class T>
class CallbackThunk<T, void>
{
public:
    CallbackThunk(std::function<void(T)> func)
        : _func(func)
    {
    }

    static void Callback(void * context, T data)
    {
        const CallbackThunk<T, void>* pthis = static_cast<CallbackThunk<T, void>*>(context);
        pthis->_func(data);
    }

private:

    std::function<void(T)> _func;
};

template <>
class CallbackThunk<void, void>
{
public:
    CallbackThunk(std::function<void()> func)
        : _func(func)
    {
    }

    static void CALLBACK Callback(void * context, bool)
    {
        const CallbackThunk<void, void>* pthis = static_cast<CallbackThunk<void, void>*>(context);
        pthis->_func();
    }

private:

    std::function<void()> _func;
};

