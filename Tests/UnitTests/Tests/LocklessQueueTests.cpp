// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "LocklessQueue.h"
#include "LocklessQueuePushParker.h"

#define TEST_CLASS_OWNER L"brianpe"

DEFINE_TEST_CLASS(LocklessQueueTests)
{
public:

#ifdef USING_TAEF

    BEGIN_TEST_CLASS(LocklessQueueTests)
    END_TEST_CLASS()

    DEFINE_TEST_METHOD_FIXTURE();

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
        std::atomic<bool> pushesDone{ false };

        for(uint32_t threadIndex = 0; threadIndex < totalPopThreads; threadIndex++)
        {
            std::thread newThread([&list, slotsPtr, &pushesDone]
            {
                // The queue can be momentarily empty while pushers are still running (e.g. when the
                // pop threads get scheduled first), so only stop once every push has finished.
                uint32_t* node;
                for (;;)
                {
                    bool const done = pushesDone.load();
                    if (!list.pop_front(node))
                    {
                        if (done)
                        {
                            break;
                        }
                        std::this_thread::yield();
                        continue;
                    }

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
        pushesDone = true;

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

    TEST_METHOD(VerifyPreemptedPushDoesNotLinkIntoAnotherList)
    {
        // Deterministic version of VerifySharedHeapPushIsNotMisrouted: park a
        // push to listA right before it links its node after listA's tail, move
        // that tail node to the tail of listB (which shares the heap), then let
        // the push finish. It must land in listA, not after the node in listB.

        std::unique_ptr<LocklessQueue<uint32_t>> listA(new LocklessQueue<uint32_t>);
        std::unique_ptr<LocklessQueue<uint32_t>> listB(new LocklessQueue<uint32_t>(*listA));

        VERIFY_IS_TRUE(listA->push_back(1));

        LocklessQueuePushParker parker;
        std::thread pusher([&]
        {
            LocklessQueuePushParker::ArmCurrentThread();
            listA->push_back(100);
        });

        bool parked = parker.WaitUntilParked(std::chrono::seconds(10));

        // listA's tail node (holding 1) stops being the tail, is popped out of
        // listA carrying 2, and becomes the tail of listB.
        uint32_t value = 0;
        uint64_t firstNode = 0;
        uint64_t tailNode = 0;
        VERIFY_IS_TRUE(listA->push_back(2));
        VERIFY_IS_TRUE(listA->pop_front(value, firstNode));
        VERIFY_ARE_EQUAL(1u, value);
        VERIFY_IS_TRUE(listA->pop_front(value, tailNode));
        VERIFY_ARE_EQUAL(2u, value);
        listA->free_node(firstNode);
        listB->push_back(value, tailNode);

        parker.Release();
        pusher.join();

        VERIFY_IS_TRUE(parked);

        VERIFY_IS_TRUE(listB->pop_front(value));
        VERIFY_ARE_EQUAL(2u, value);
        bool const misrouted = listB->pop_front(value);
        VERIFY_IS_FALSE(misrouted);

        VERIFY_IS_TRUE(listA->pop_front(value));
        VERIFY_ARE_EQUAL(100u, value);
        VERIFY_IS_TRUE(listA->empty());
    }

    TEST_METHOD(VerifySharedHeapPushIsNotMisrouted)
    {
        // Regression: push_back reads the tail node's "next" field, verifies it is the
        // end marker and then CASes the new node in. Every list sharing a heap used the
        // same end marker, so if the pushing thread was preempted between those steps
        // while the tail node was popped and pushed to another list sharing the heap
        // (the task queue moves nodes between its pending, ready and active lists), the
        // stale CAS still succeeded and linked the new item into the *other* list. The
        // task queue lost delayed callbacks this way: XTaskQueueSubmitDelayedCallback
        // returned S_OK but the callback never ran.
        //
        // Producers push to listA, movers transfer nodes from listA to listB (sharing
        // the heap), consumers pop listB. A consumed value that no mover transferred
        // was misrouted. Threads are oversubscribed to force preemption inside push.

        const uint32_t hw = std::max(2u, std::thread::hardware_concurrency());
        const uint32_t producerCount = hw;
        const uint32_t moverCount = hw;
        const uint32_t consumerCount = hw;
        const uint64_t maxPerProducer = 1000000;
        const auto timeLimit = std::chrono::seconds(3);
        const uint64_t slotCount = producerCount * maxPerProducer;

        std::unique_ptr<std::atomic<uint8_t>[]> moved(new std::atomic<uint8_t>[slotCount]);
        std::unique_ptr<std::atomic<uint8_t>[]> consumed(new std::atomic<uint8_t>[slotCount]);
        for (uint64_t idx = 0; idx < slotCount; idx++)
        {
            moved[idx] = 0;
            consumed[idx] = 0;
        }

        std::unique_ptr<LocklessQueue<uint64_t>> listA(new LocklessQueue<uint64_t>);
        std::unique_ptr<LocklessQueue<uint64_t>> listB(new LocklessQueue<uint64_t>(*listA));

        std::atomic<bool> stop{ false };
        std::atomic<uint32_t> producersRunning{ producerCount };
        std::atomic<uint32_t> moversRunning{ moverCount };
        std::atomic<uint64_t> produced{ 0 };
        std::atomic<uint64_t> consumedCount{ 0 };
        std::atomic<uint64_t> misrouted{ 0 };
        std::atomic<uint64_t> duplicates{ 0 };
        std::atomic<uint64_t> pushFailures{ 0 };

        auto consume = [&](uint64_t value)
        {
            if (moved[value].load() == 0)
            {
                misrouted++;
                stop = true;
            }
            if (consumed[value].exchange(1) != 0)
            {
                duplicates++;
                stop = true;
            }
            consumedCount++;
        };

        std::vector<std::thread> threads;

        for (uint32_t producer = 0; producer < producerCount; producer++)
        {
            threads.emplace_back([&, producer]
            {
                for (uint64_t idx = 0; idx < maxPerProducer && !stop.load(std::memory_order_relaxed); idx++)
                {
                    if (!listA->push_back(producer * maxPerProducer + idx))
                    {
                        pushFailures++;
                        break;
                    }
                    produced++;
                }
                producersRunning--;
            });
        }

        for (uint32_t mover = 0; mover < moverCount; mover++)
        {
            threads.emplace_back([&]
            {
                uint64_t value;
                uint64_t address;
                for (;;)
                {
                    bool const producing = producersRunning.load() != 0;
                    if (listA->pop_front(value, address))
                    {
                        moved[value] = 1;
                        listB->push_back(value, address);
                    }
                    else if (!producing)
                    {
                        break;
                    }
                }
                moversRunning--;
            });
        }

        for (uint32_t consumer = 0; consumer < consumerCount; consumer++)
        {
            threads.emplace_back([&]
            {
                uint64_t value;
                for (;;)
                {
                    bool const moving = moversRunning.load() != 0;
                    if (listB->pop_front(value))
                    {
                        consume(value);
                    }
                    else if (!moving)
                    {
                        break;
                    }
                }
            });
        }

        auto const start = std::chrono::steady_clock::now();
        while (producersRunning.load() != 0)
        {
            if (std::chrono::steady_clock::now() - start > timeLimit)
            {
                stop = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        // A misrouted push can also land in listB after the consumers exit.
        uint64_t value;
        while (listB->pop_front(value))
        {
            consume(value);
        }
        uint64_t leftInA = 0;
        while (listA->pop_front(value))
        {
            leftInA++;
        }

        const uint64_t none = 0;
        VERIFY_ARE_EQUAL(none, pushFailures.load());
        VERIFY_ARE_EQUAL(none, misrouted.load());
        VERIFY_ARE_EQUAL(none, duplicates.load());
        VERIFY_ARE_EQUAL(none, leftInA);
        VERIFY_ARE_EQUAL(produced.load(), consumedCount.load());
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


    TEST_METHOD(VerifyRemoveIf)
    {
        LocklessQueue<int> list;
        constexpr int total = 20;
        for (int idx = 0; idx < total; idx++)
        {
            list.push_back(idx);
        }

        // We will create 10 threads. Each thread has a
        // target index it removes. When done, all even
        // numbers should be removed.

        std::thread threads[total / 2];

        for(int threadIndex = 0; threadIndex < total / 2; threadIndex++)
        {
            std::thread newThread([threadIndex, &list]
            {
                list.remove_if([threadIndex, &list](auto& value, auto& address)
                {
                    if (value == threadIndex * 2)
                    {
                        list.free_node(address);
                        return true;
                    }
                    return false;
                });
            });
            threads[threadIndex].swap(newThread);
        }

        for (int threadIndex = 0; threadIndex < total / 2; threadIndex++)
        {
            threads[threadIndex].join();
        }

        // There should only be odd items left

        int value;
        while (list.pop_front(value))
        {
            VERIFY_IS_TRUE(value % 2 != 0);
        }
    }
};
