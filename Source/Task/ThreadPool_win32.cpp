#include "pch.h"
#include "ThreadPool.h"

namespace OS
{
    class ThreadPoolImpl
    {
    public:

        ~ThreadPoolImpl() noexcept
        {
            Terminate();
        }

        void AddRef()
        {
            m_refs++;
        }

        void Release()
        {
            if (--m_refs == 0)
            {
                delete this;
            }
        }

        HRESULT Initialize(
            _In_opt_ void* context,
            _In_ ThreadPoolCallback* callback) noexcept
        {
            m_context = context;
            m_callback = callback;
            m_work = CreateThreadpoolWork(TPCallback, this, nullptr);
            RETURN_LAST_ERROR_IF_NULL(m_work);

            return S_OK;
        }

        void Terminate() noexcept
        {
            if (m_work != nullptr)
            {
                // Wait for callbacks to complete.  We don't want to
                // cancel existing callbacks because it's important they
                // get a chance to drain.
                WaitForThreadpoolWorkCallbacks(m_work, FALSE);
                CloseThreadpoolWork(m_work);
                m_work = nullptr;
            }
        }

        void Submit() noexcept
        {
            SubmitThreadpoolWork(m_work);
        }

    private:

        static void CALLBACK TPCallback(
            _In_ PTP_CALLBACK_INSTANCE instance,
            _In_ void* context, PTP_WORK) noexcept
        {
            ThreadPoolImpl* pthis = static_cast<ThreadPoolImpl*>(context);

            // ActionComplete is an optional call
            // the callback can make to indicate 
            // all portions of the call have finished
            // and it is safe to release the
            // thread pool, even if the callback has
            // not totally unwound.  This is neccessary
            // to allow users to close a task queue from
            // within a callback.  Task queue guards with an 
            // extra ref to ensure a safe point where 
            // member state is no longer accessed, but the
            // final release does need to wait on outstanding
            // calls.

            ActionCompleteImpl ac(pthis, instance);
            pthis->AddRef();
            pthis->m_callback(pthis->m_context, ac);

            if (!ac.Invoked)
            {
                ac();
            }
            pthis->Release(); // May delete this
        }

        struct ActionCompleteImpl : ThreadPoolActionComplete
        {
            ActionCompleteImpl(ThreadPoolImpl* owner, PTP_CALLBACK_INSTANCE instance) :
                m_owner(owner),
                m_instance(instance)
            {
            }

            bool Invoked = false;

            void operator()() override
            {
                Invoked = true;
                DisassociateCurrentThreadFromCallback(m_instance);
            }

        private:
            ThreadPoolImpl* m_owner = nullptr;
            PTP_CALLBACK_INSTANCE m_instance = nullptr;
        };

        std::atomic<uint32_t> m_refs{ 1 };
        PTP_WORK m_work = nullptr;
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
            m_impl->Release();
            m_impl = nullptr;
        }
    }

    void ThreadPool::Submit()
    {
        m_impl->Submit();
    }

} // Namespace