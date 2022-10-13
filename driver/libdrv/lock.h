/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

class Lock
{
public:
        Lock(KSPIN_LOCK &lock) { KeAcquireInStackQueuedSpinLock(&lock, &m_hlock); }
        ~Lock() { release(); }

        void release()
        {
                if (InterlockedExchange8(&m_acquired, false)) {
                        KeReleaseInStackQueuedSpinLock(&m_hlock); 
                }
        }

private:
        KLOCK_QUEUE_HANDLE m_hlock{};
        CHAR m_acquired = true;
};
