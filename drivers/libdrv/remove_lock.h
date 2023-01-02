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
        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock) : m_lock(&lock) 
        {
                if (auto err = IoAcquireRemoveLock(m_lock, nullptr)) {
                        NT_ASSERT(err == STATUS_DELETE_PENDING);
                        m_lock = nullptr;
                }
        }

        ~RemoveLockGuard() 
        {
                if (m_lock) {
                        IoReleaseRemoveLock(m_lock, nullptr);
                }
        }

        auto acquired() const { return m_lock ? STATUS_SUCCESS : STATUS_DELETE_PENDING; }

        void release_and_wait()
        {
                if (m_lock) {
                        IoReleaseRemoveLockAndWait(m_lock, nullptr);
                        m_lock = nullptr;
                }
        }

        RemoveLockGuard(const RemoveLockGuard&) = delete;
        RemoveLockGuard& operator =(const RemoveLockGuard&) = delete;

private:
        IO_REMOVE_LOCK *m_lock{};
};

} // namespace libdrv
