// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignas

/*****************************************************************************
 
 LocklessList is a lock free singly linked list that can take any type as a payload.
 For a given payload type, it defines a type "Node" that represents a node in
 the linked list.  Node contains your payload as a "data" member and an atomic
 uintptr_t next value.  Note that extra data is stored in the low nibble of next so
 it is not directly castable to the next node pointer.
 
 LocklessList allocates its own node pointers, but you may also pass them as
 parameters.
 
 Typical Usage:
 
 struct MyPayload
 {
 uint32_t someData;
 // etc...
 };
 
 LocklessList<MyPayload> list;
 
 MyPayload p = new MyPayload;
 p->someData = 16;
 list.push_back(p);
 
 p = list.pop_front();
 if (p != nullptr)
 {
 printf("Value: %d\r\n", p->data.someData);
 delete p;
 }

 LocklessList is based on the the algorithm described in this paper:

 Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms

 Maged M. Michael Michael L. Scott
 Department of Computer Science University of Rochester Rochester, NY 14627-0226 fmichael,scottg@cs.rochester.ed

 http://cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
 
 ******************************************************************************/

template <typename TData>
class alignas(sizeof(std::uintptr_t) * 8) LocklessList
{
public:
    
    struct alignas(sizeof(std::uintptr_t) * 8) Node
    {
        std::atomic<std::uintptr_t> next;
        TData* data;

        Node() :
            next(0),
            data(nullptr) {}
        
        static void* operator new(size_t sz)
        {
            void* ptr = aligned_malloc(sz, sizeof(std::uintptr_t) * 8);
            if (ptr == nullptr)
            {
                throw new std::bad_alloc;
            }
            return ptr;
        }
        
        static void* operator new(size_t sz, const std::nothrow_t&)
        {
            return aligned_malloc(sz, sizeof(std::uintptr_t) * 8);
        }
        
        static void operator delete(void* ptr)
        {
            aligned_free(ptr);
        }

        static void operator delete(void* ptr, const std::nothrow_t&)
        {
            aligned_free(ptr);
        }
    };
    
    static void* operator new(size_t sz)
    {
        void* ptr = aligned_malloc(sz, sizeof(std::uintptr_t) * 8);
        if (ptr == nullptr)
        {
            throw new std::bad_alloc;
        }
        return ptr;
    }
    
    static void* operator new(size_t sz, const std::nothrow_t&)
    {
        return aligned_malloc(sz, sizeof(std::uintptr_t) * 8);
    }
    
    static void operator delete(void* ptr)
    {
        aligned_free(ptr);
    }
    
    LocklessList() noexcept
    {
        m_head = reinterpret_cast<std::uintptr_t>(&m_initialNode);
        m_tail = reinterpret_cast<std::uintptr_t>(&m_initialNode);;
        ASSERT((m_head & 0x1F) == 0); // Alignment problem
    }
    
    ~LocklessList() noexcept
    {
        // If the list is not empty at destruction it leaks.  If
        // empty we still have one node left that may have been
        // dynamically allocated. 
        if (ToNode(m_head) != &m_initialNode)
        {
            delete ToNode(m_head);
        }
    }
    
    // Returns true if we believe the list is empty.
    bool empty() noexcept
    {
        return ToNode(m_head) == ToNode(m_tail);
    }
    
    // Pushes a new element on the list. If node is null, it will
    // be allocated.  push_back returns false if out of memory as
    // that is the only error condition.
    bool push_back(_In_ TData* data, _In_opt_ Node* node = nullptr) noexcept
    {
        if (node == nullptr)
        {
            node = new (std::nothrow) Node;
        }
        
        if (node == nullptr)
        {
            return false;
        }

        ASSERT((reinterpret_cast<std::uintptr_t>(node) & 0x1F) == 0); // Alignment problem

        node->next = 0;
        node->data = data;
        
        std::uintptr_t localTail = 0;
        std::uintptr_t localNext = 0;
        
        // we have to loop around until we successfully insert the node as the last element in the queue
        for (;;)
        {
            localTail = m_tail;

            if (!IsPtrLocked(localTail) && TryGetNextPtr(m_tail, localTail, localNext))
            {
                if (ToNode(localNext) == nullptr)
                {
                    // attempt to insert the element at the end of the queue
                    // this is only valid if the tail of the queue is still pointing to the last element in the queue
                    std::uintptr_t temp = ToPtr(localNext, node);
                    if (ToNode(localTail)->next.compare_exchange_weak(localNext, temp))
                    {
                        // we were successful in inserting the element into the queue
                        // we can now break out of the loop trying to insert the element
                        // at this point m_tail->next is pointing to the new node
                        // however m_tail is not pointing to the new node yet
                        break;
                    }
                }
                // m_tail not pointing to last node in list, so we need to try and move it along the list until it is
                // we don't care if the interlock fails, we'll just catch it the next time around
                else
                {
                    std::uintptr_t temp = ToPtr(localTail, ToNode(localNext));
                    m_tail.compare_exchange_weak(localTail, temp);
                }
            }
        }
        
        // The node has been inserted into the last element in the list, we now need to update m_tail to point to it
        // we don't care if the interlock fails, if so we'll just catch it next time around in either push or pop
        std::uintptr_t temp = ToPtr(localTail, node);
        m_tail.compare_exchange_weak(localTail, temp);
        return true;
    }
    
    // Returns pointer to head, or null if nothing
    // in queue.  You own TData now.  The list node
    // will be deleted unless a valid node pointer
    // is provided.
    TData* pop_front(Node** node = nullptr) noexcept
    {
        if (node != nullptr)
        {
            *node = nullptr;
        }
        
        TData* data = nullptr;
        std::uintptr_t localHead = 0;
        std::uintptr_t localTail = 0;
        std::uintptr_t localNext = 0;
        
        for (;;)
        {
            // We need to make local copies of the head and tail pointers for later checks in the interlocks
            // since head/tail can never be null we don't need any check here for that.
            // The queue is empty if m_head == m_tail
            localHead = m_head;
            localTail = m_tail;

            if (!IsPtrLocked(localHead) && TryGetNextPtr(m_head, localHead, localNext))
            {
                // in our captured state m_head == m_tail, this means we think the queue is empty
                // however it may not be since m_tail is not always pointing to the real last element in the queue
                if (ToNode(localHead) == ToNode(localTail))
                {
                    // If next is null we are empty.
                    if (ToNode(localNext) == nullptr)
                    {
                        return nullptr;
                    }
                    
                    // The queue is not really empty so we need to move m_tail to the next element in the queue
                    // This could happen on multiple times since we can safely only move it by one element.
                    if (!IsPtrLocked(localTail))
                    {
                        std::uintptr_t temp = ToPtr(localTail, ToNode(localNext));
                        m_tail.compare_exchange_weak(localTail, temp);
                    }
                }
                else
                {
                    // The queue is not empty.  Lock the head, extract the data, then move to the
                    // next node.
                    std::uintptr_t lockedHead = LockPtr(localHead);
                    if (m_head.compare_exchange_weak(localHead, lockedHead))
                    {
                        Node* nextNode = ToNode(localNext);
                        data = nextNode->data;
                        std::uintptr_t temp = ToPtr(localHead, nextNode);
                        
                        if (m_head.compare_exchange_strong(lockedHead, temp))
                        {
                            break;
                        }
                        else
                        {
                            // This should never happen because our lock should
                            // have prevented races.  Remove the lock
                            ASSERT(false);
                            m_head.compare_exchange_strong(lockedHead, localHead);
                        }
                    }
                }
            }
        }
        
        // Delete the disconnected node now unless it is
        // m_initialNode
        Node* headNode = ToNode(localHead);
        if (headNode != &m_initialNode)
        {
            if (node != nullptr)
            {
                *node = headNode;
            }
            else
            {
                delete headNode;
            }
        }
        
        return data;
    }
    
private:
    
    // We keep a single additional node around
    Node m_initialNode = { };
    
    // List head and tail
    alignas(sizeof(std::uintptr_t) * 8) std::atomic<std::uintptr_t> m_head;
    alignas(sizeof(std::uintptr_t) * 8) std::atomic<std::uintptr_t> m_tail;
    
    inline Node* ToNode(std::uintptr_t ptr)
    {
        std::uintptr_t mask = 0x1f;
        return reinterpret_cast<Node*>(ptr & ~mask);
    }
    
    // Converts a node to a uintptr_t. This takes an existing node
    // which we use to grab it's count in the low nibble and increment
    // it.  This technique keeps pointers somewhat unique to prevent
    // ABA problems.
    inline std::uintptr_t ToPtr(std::uintptr_t basis, Node* node)
    {
        std::uintptr_t mask = 0xf;
        std::uintptr_t cnt = basis & mask;
        cnt = (cnt + 1) & mask;
        
        std::uintptr_t ptr = reinterpret_cast<std::uintptr_t>(node);
        return ptr | cnt;
    }

    // We allow a lock bit in bit zero of the second nibble.  When 
    // locked, hands off!
    inline std::uintptr_t LockPtr(std::uintptr_t ptr)
    {
        return ptr | 0x10;
    }

    inline std::uintptr_t UnlockPtr(std::uintptr_t ptr)
    {
        std::uintptr_t mask = 0x10;
        return ptr & ~mask;
    }

    inline bool IsPtrLocked(std::uintptr_t ptr)
    {
        return (ptr & 0x10) != 0;
    }

    inline bool TryGetNextPtr(std::atomic<std::uintptr_t>& ptr, std::uintptr_t localPtr, std::uintptr_t& nextPtr)
    {
        // This attempts to get the next ptr from the given
        // node ptr by doing a temporary lock/unlock

        std::uintptr_t expected = UnlockPtr(localPtr);
        std::uintptr_t locked = LockPtr(expected);

        if (ptr.compare_exchange_weak(expected, locked))
        {
            nextPtr = ToNode(locked)->next;
            ptr.compare_exchange_strong(locked, expected);
            return true;
        }

        return false;
    }
    
    static inline void* aligned_malloc(size_t size, size_t align)
    {
        void *result;
        size_t bytes = (size + align - 1) & ~(align - 1);
#ifdef _MSC_VER
        result = _aligned_malloc(bytes, align);
#else
        if(posix_memalign(&result, align, bytes)) result = 0;
#endif
        return result;
    }
    
    static inline void aligned_free(void *ptr)
    {
#ifdef _MSC_VER
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }
};

#pragma warning(pop)
