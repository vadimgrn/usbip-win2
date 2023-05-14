/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

namespace wdm
{

class Lock
{
public:
        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_saves_global_(QueuedSpinLock,m_hlock)
        _IRQL_raises_(DISPATCH_LEVEL)
        Lock(_Inout_ KSPIN_LOCK &lock)
        { 
                KeAcquireInStackQueuedSpinLock(&lock, &m_hlock); 
        }

        _IRQL_requires_(DISPATCH_LEVEL)
        _IRQL_restores_global_(QueuedSpinLock,m_hlock) // FIXME: annotations for destructors are ignored
        ~Lock() { release(); }

        Lock(_In_ const Lock&) = delete;
        Lock& operator=(_In_ const Lock&) = delete;

        _IRQL_requires_(DISPATCH_LEVEL)
        _IRQL_restores_global_(QueuedSpinLock,m_hlock)
        void release()
        {
                if (InterlockedExchange8(PCHAR(&m_acquired), false)) {
                        static_assert(sizeof(m_acquired) == sizeof(CHAR));
                        KeReleaseInStackQueuedSpinLock(&m_hlock); 
                }
        }

private:
        KLOCK_QUEUE_HANDLE m_hlock{};
        bool m_acquired = true;
};

} // namespace wdm
