/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

class Lock
{
public:
        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_raises_(DISPATCH_LEVEL)
        Lock(_Inout_ KSPIN_LOCK &lock) 
        { 
                KeAcquireInStackQueuedSpinLock(&lock, &m_hlock); 
                NT_ASSERT(acquired());
        }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~Lock() { release(); }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void release()
        {
                if (acquired()) {
                        KeReleaseInStackQueuedSpinLock(&m_hlock); 
                        m_hlock.LockQueue.Lock = 0;
                        NT_ASSERT(!acquired());
                }
        }

private:
        KLOCK_QUEUE_HANDLE m_hlock;
        bool acquired() const { return m_hlock.LockQueue.Lock; }
};
