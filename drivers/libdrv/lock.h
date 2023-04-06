/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

class Lock
{
public:
        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_saves_global_(QueuedSpinLock,m_hlock)
        _IRQL_raises_(DISPATCH_LEVEL)
        Lock(_Inout_ KSPIN_LOCK &lock)
        { 
                KeAcquireInStackQueuedSpinLock(&lock, &m_hlock); 
                NT_ASSERT(acquired());
        }

        _IRQL_requires_(DISPATCH_LEVEL)
        _IRQL_restores_global_(QueuedSpinLock,m_hlock) // FIXME: annotations for destructors are ignored
        ~Lock() { release(); }

        _IRQL_requires_(DISPATCH_LEVEL)
        _IRQL_restores_global_(QueuedSpinLock,m_hlock)
        void release()
        {
                if (acquired()) {
                        KeReleaseInStackQueuedSpinLock(&m_hlock); 
                        m_hlock.LockQueue.Lock = nullptr;
                        NT_ASSERT(!acquired());
                }
        }

private:
        KLOCK_QUEUE_HANDLE m_hlock{};
        bool acquired() const { return m_hlock.LockQueue.Lock; }
};
