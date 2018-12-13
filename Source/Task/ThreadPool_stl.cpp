#include "pch.h"
#include "ThreadPool.h"

class ThreadPoolImpl
{
public:

    ~ThreadPoolImpl() noexcept
    {
        Terminate();
    }

    HRESULT Initialize(
        _In_opt_ void* context,
        _In_ ThreadPoolCallback* callback) noexcept
    {
        m_context = context;
        m_callback = callback;

        uint32_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0)
        {
            numThreads = 1;
        }

        try
        {
            while(numThreads != 0)
            {
                numThreads--;
                m_pool.emplace_back(std::thread([this]
                {
                    std::unique_lock<std::mutex> lock(m_wakeLock);
                    while(true)
                    {
                        m_wake.wait(lock);

                        if (m_terminate)
                        {
                            break;
                        }

                        if (m_calls != 0)
                        {
                            m_calls--;

                            lock.unlock();
                            m_callback(m_context);
                            lock.lock();
                        }
                    }
                }));
            }
        }
        catch(const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    void Terminate() noexcept
    {
        m_terminate = true;
        m_wake.notify_all();

        for(auto &thread : m_pool)
        {
            thread.join();
        }

        m_pool.clear();
    }

    void Submit() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(m_wakeLock);
            m_calls++;
        }
        m_wake.notify_all();
    }

private:

    std::mutex m_wakeLock;
    std::condition_variable m_wake;
    bool m_terminate = false;
    std::vector<std::thread> m_pool;
    uint32_t m_calls = 0;
    void* m_context = nullptr;
    ThreadPoolCallback* m_callback = nullptr;
};

ThreadPool::ThreadPool() noexcept :
    m_impl(nullptr)
{
}

ThreadPool::~ThreadPool() noexcept
{
    Terminate();
}

HRESULT ThreadPool::Initialize(_In_opt_ void* context, _In_ ThreadPoolCallback* callback) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_impl != nullptr);
    
    std::unique_ptr<ThreadPoolImpl> impl(new (std::nothrow) ThreadPoolImpl);
    RETURN_IF_NULL_ALLOC(impl);

    RETURN_IF_FAILED(impl->Initialize(context, callback));

    m_impl = impl.release();
    return S_OK;
}

void ThreadPool::Terminate() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Terminate();
        delete m_impl;
        m_impl = nullptr;
    }
}

void ThreadPool::Submit()
{
    m_impl->Submit();
}
