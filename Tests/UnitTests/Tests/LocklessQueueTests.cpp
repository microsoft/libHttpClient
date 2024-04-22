// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "LocklessQueue.h"

#define TEST_CLASS_OWNER L"brianpe"

DEFINE_TEST_CLASS(LocklessQueueTests)
{
public:

#ifdef USING_TAEF

    BEGIN_TEST_CLASS(LocklessQueueTests)
    END_TEST_CLASS()

#else
    DEFINE_TEST_CLASS_PROPS(LocklessQueueTests);
#endif

    TEST_METHOD(VerifyBasicOps)
    {
        const uint32_t opCount = 2;
        std::unique_ptr<bool[]> ops(new bool[opCount]);
        memset(ops.get(), 0, sizeof(bool) * opCount);

        LocklessQueue<uint32_t*> list;
        VERIFY_IS_TRUE(list.empty());

        for(uint32_t idx = 0; idx < opCount; idx++)
        {
            auto node = new uint32_t;
            *node = idx;
            VERIFY_IS_TRUE(list.push_back(node));
            VERIFY_IS_FALSE(list.empty());
        }

        while(true)
        {
            bool wasEmpty = list.empty();
            uint32_t* node;

            if (!list.pop_front(node))
            {
                VERIFY_IS_TRUE(wasEmpty);
                break;
            }

            VERIFY_IS_FALSE(wasEmpty);

            ops[*node] = true;
            delete node;
        }

        for (uint32_t idx = 0; idx < opCount; idx++)
        {
            VERIFY_IS_TRUE(ops[idx]);
        }
    }

    TEST_METHOD(VerifySeveralThreads)
    {
        const uint32_t totalPushThreads = 30;
        const uint32_t totalPopThreads = 10;
        const uint32_t callsPerThread = 50000;

        std::unique_ptr<bool[]> slots(new bool[totalPushThreads * callsPerThread]);
        memset(slots.get(), 0, sizeof(bool) * totalPushThreads * callsPerThread);

        //while(!IsDebuggerPresent()) Sleep(1000);

        LocklessQueue<uint32_t*> list;

        std::thread pushThreads[totalPushThreads];

        bool* slotsPtr = slots.get();

        for(uint32_t threadIndex = 0; threadIndex < totalPushThreads; threadIndex++)
        {
            std::thread newThread([threadIndex, &list, slotsPtr, callsPerThread]
            {
                for(uint32_t callIndex = 0; callIndex < callsPerThread; callIndex++)
                {
                    uint32_t* node = new uint32_t;
                    *node = callIndex + (threadIndex * callsPerThread);
                    if (slotsPtr[*node])
                    {
                        // Way too much contention in the logging system to verify directly
                        VERIFY_FAIL();
                    }
                    if (!list.push_back(node))
                    {
                        VERIFY_FAIL();
                    }
                }
            });
            pushThreads[threadIndex].swap(newThread);
        }

        std::thread popThreads[totalPopThreads];

        for(uint32_t threadIndex = 0; threadIndex < totalPopThreads; threadIndex++)
        {
            std::thread newThread([&list, slotsPtr]
            {
                uint32_t* node;
                while(list.pop_front(node))
                {
                    if (slotsPtr[*node])
                    {
                        // Way too much contention in the logging system to verify directly
                        VERIFY_FAIL();
                    }
                    slotsPtr[*node] = true;
                    delete node;
                }
            });
            popThreads[threadIndex].swap(newThread);
        }

        // Now we have a massive race between push and pop.  Wait for all the pushes to be done.

        for(uint32_t threadIndex = 0; threadIndex < totalPushThreads; threadIndex++)
        {
            pushThreads[threadIndex].join();
        }

        // And now that all the pushes are complete, wait on the pops.

        for(uint32_t threadIndex = 0; threadIndex < totalPopThreads; threadIndex++)
        {
            popThreads[threadIndex].join();
        }

        // Now that we're done, verify each call made it.

        for(uint32_t callIndex = 0; callIndex < totalPushThreads * callsPerThread; callIndex++)
        {
            if (!slots[callIndex])
            {
                VERIFY_FAIL();
            }
        }
    }

    TEST_METHOD(VerifyOutputNodes)
    {
        const uint32_t opCount = 2;
        std::unique_ptr<bool[]> ops(new bool[opCount]);
        memset(ops.get(), 0, sizeof(bool) * opCount);

        LocklessQueue<uint32_t*> list1;

        for(uint32_t idx = 0; idx < opCount; idx++)
        {
            auto value = new uint32_t;
            *value = idx;
            VERIFY_IS_TRUE(list1.push_back(value));
        }

        // Setup list2 to use the same node heap as list1
        // so they can share nodes.
        LocklessQueue<uint32_t*> list2(list1);

        while(true)
        {
            std::uint64_t node;
            std::uint32_t* value;

            if (!list1.pop_front(value, node))
            {
                break;
            }

            list2.push_back(value, node);
        }

        while(true)
        {
            std::uint32_t* value;

            if (!list2.pop_front(value))
            {
                break;
            }

            ops[*value] = true;
            delete value;
        }

        for (uint32_t idx = 0; idx < opCount; idx++)
        {
            VERIFY_IS_TRUE(ops[idx]);
        }
    }

    TEST_METHOD(VerifyRemoveAndReAdd)
    {
        LocklessQueue<uint32_t*> list;
        const uint32_t total = 10;

        for (uint32_t idx = 0; idx < total; idx++)
        {
            uint32_t* n = new uint32_t(idx);
            VERIFY_IS_TRUE(list.push_back(n));
        }

        uint32_t* initial = nullptr;
        uint32_t evenCount = 0;
        uint32_t evenPushCount = 0;
        uint32_t oddCount = 0;
        uint32_t* node;

        while (list.pop_front(node))
        {
            if (node == initial)
            {
                list.push_back(node);
                break;
            }

            if ((*node) & 1)
            {
                oddCount++;
                delete node;
            }
            else
            {
                if (initial == nullptr)
                {
                    initial = node;
                }
                list.push_back(node);
                evenPushCount++;
            }
        }

        // Now pop off what's left - they should be all the
        // evens.
        while (list.pop_front(node))
        {
            VERIFY_ARE_EQUAL(0u, ((*node) & 1u));
            delete node;
            evenCount++;
        }

        VERIFY_ARE_EQUAL(oddCount, evenPushCount);
        VERIFY_ARE_EQUAL(oddCount, evenCount);
    }
};
