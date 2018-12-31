
// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

// A wrapper around a vector that allows lock-free enumeration of the
// contents.  Add/Remove requires a lock.
template <class TElement>
class AtomicVector
{
public:

    typedef void VisitCallback(_In_ void*, _In_ TElement);
    typedef bool RemovePredicate(_In_ void*, _In_ TElement);

    HRESULT Add(_In_ const TElement value) try
    {
        std::lock_guard<std::mutex> lock(m_lock);
        uint32_t bufferReadIdx = (m_indexAndRef & 0x80000000 ? 1 : 0);
        uint32_t bufferWriteIdx = 1 - bufferReadIdx;

        m_buffers[bufferWriteIdx] = m_buffers[bufferReadIdx];
        m_buffers[bufferWriteIdx].emplace_back(value);

        // Now spin wait to swap the active buffer.
        uint32_t expected = bufferReadIdx << 31;
        uint32_t desired = bufferWriteIdx << 31;

        while (!m_indexAndRef.compare_exchange_weak(expected, desired)) {}

        return S_OK;
    } CATCH_RETURN();

    void Remove(_In_ void* context, _In_ RemovePredicate* predicate)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        uint32_t bufferReadIdx = (m_indexAndRef & 0x80000000 ? 1 : 0);
        uint32_t bufferWriteIdx = 1 - bufferReadIdx;

        m_buffers[bufferWriteIdx] = m_buffers[bufferReadIdx];

        for (auto it = m_buffers[bufferWriteIdx].begin(); it != m_buffers[bufferWriteIdx].end(); it++)
        {
            if (predicate(context, *it))
            {
                m_buffers[bufferWriteIdx].erase(it);
                break;
            }
        }

        // Now spin wait to swap the active buffer.
        uint32_t expected = bufferReadIdx << 31;
        uint32_t desired = bufferWriteIdx << 31;

        while (!m_indexAndRef.compare_exchange_weak(expected, desired)) {}
    }

    void Visit(_In_ void* context, _In_ VisitCallback* callback)
    {
        uint32_t indexAndRef = ++m_indexAndRef;
        uint32_t bufferIdx = (indexAndRef & 0x80000000 ? 1 : 0);

        for (auto const &it : m_buffers[bufferIdx])
        {
            callback(context, it);
        }

        m_indexAndRef--;
    }

private:

    std::mutex m_lock;
    std::vector<TElement> m_buffers[2];
    std::atomic<uint32_t> m_indexAndRef = { 0 };
};
