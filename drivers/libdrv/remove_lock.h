/*
* Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>

namespace libdrv
{

class RemoveLockGuard
{
public:
        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock) : 
                m_result(IoAcquireRemoveLock(&lock, nullptr)),
                m_lock(NT_SUCCESS(m_result) ? &lock : nullptr) {}

        ~RemoveLockGuard() 
        {
                if (m_lock) {
                        IoReleaseRemoveLock(m_lock, nullptr);
                }
        }

        RemoveLockGuard(const RemoveLockGuard&) = delete;
        RemoveLockGuard& operator =(const RemoveLockGuard&) = delete;

        auto acquired() const { return m_result; }

        void release_and_wait()
        {
                if (m_lock) {
                        IoReleaseRemoveLockAndWait(m_lock, nullptr);
                        m_lock = nullptr;
                }
        }

private:
        NTSTATUS m_result;
        IO_REMOVE_LOCK *m_lock;
};

} // namespace libdrv
