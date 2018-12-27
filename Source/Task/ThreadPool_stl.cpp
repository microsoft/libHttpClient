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

                            ActionCompleteImpl ac(this);

                            lock.unlock();
                            m_callback(m_context, ac);
                            lock.lock();

                            if (!ac.Invoked)
                            {
                                m_activeCalls--;
                                m_active.notify_all();
                            }
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

        // Wait for the active call count
        // to go to zero.
        std::unique_lock<std::mutex> lock(m_activeLock);
        while (m_activeCalls != 0)
        {
            m_active.wait(lock);
        }
        lock.unlock();

        m_pool.clear();
    }

    void Submit() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(m_wakeLock);
            m_calls++;
            m_activeCalls++;
        }
        m_wake.notify_all();
    }

private:

    struct ActionCompleteImpl : ThreadPoolActionComplete
    {
        ActionCompleteImpl(ThreadPoolImpl* owner) :
            m_owner(owner)
        {
        }

        bool Invoked = false;

        void operator()() override
        {
            Invoked = true;
            m_owner->m_activeCalls--;
            m_owner->m_active.notify_all();
        }

    private:
        ThreadPoolImpl * m_owner = nullptr;
    };

    std::mutex m_wakeLock;
    std::condition_variable m_wake;
    uint32_t m_calls = 0;

    std::mutex m_activeLock;
    std::condition_variable m_active;
    uint32_t m_activeCalls = 0;

    bool m_terminate = false;
    std::vector<std::thread> m_pool;
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
