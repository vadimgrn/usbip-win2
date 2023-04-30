/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace libdrv
{

struct adopt_lock_t {};
inline constexpr adopt_lock_t adopt_lock;

class RemoveLockGuard
{
public:
        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock) : 
                m_result(IoAcquireRemoveLock(&lock, nullptr)),
                m_lock(NT_SUCCESS(m_result) ? &lock : nullptr) {}

        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock, _In_ adopt_lock_t) : m_lock(&lock) {}

        ~RemoveLockGuard() 
        {
                if (m_lock) {
                        IoReleaseRemoveLock(m_lock, nullptr);
                }
        }

        RemoveLockGuard(const RemoveLockGuard&) = delete;
        RemoveLockGuard& operator =(const RemoveLockGuard&) = delete;

        auto acquired() const { return m_result; }
        
        void clear() 
        { 
                m_result = STATUS_DEVICE_DOES_NOT_EXIST;
                m_lock = nullptr; 
        }

        void release_and_wait()
        {
                NT_ASSERT(m_lock);
                IoReleaseRemoveLockAndWait(m_lock, nullptr);
                clear();
        }

private:
        NTSTATUS m_result = STATUS_SUCCESS;
        IO_REMOVE_LOCK *m_lock{};
};

} // namespace libdrv
