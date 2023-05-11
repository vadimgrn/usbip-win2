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
        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock, _In_opt_ void *tag = nullptr) : 
                m_acquired(IoAcquireRemoveLock(&lock, tag)),
                m_lock(NT_SUCCESS(m_acquired) ? &lock : nullptr),
                m_tag(tag) {}

        RemoveLockGuard(_In_ IO_REMOVE_LOCK &lock, _In_ adopt_lock_t, _In_opt_ void *tag = nullptr) : 
                m_lock(&lock), m_tag(tag) {}

        ~RemoveLockGuard() 
        {
                if (m_lock) {
                        IoReleaseRemoveLock(m_lock, m_tag);
                }
        }

        RemoveLockGuard(const RemoveLockGuard&) = delete;
        RemoveLockGuard& operator =(const RemoveLockGuard&) = delete;

        auto acquired() const { return m_acquired; }
        auto tag() const { return m_tag; }

        void clear() 
        { 
                m_acquired = STATUS_INVALID_ADDRESS;
                m_lock = nullptr; 
                m_tag = nullptr; 
        }

        void release_and_wait()
        {
                NT_ASSERT(m_lock);
                IoReleaseRemoveLockAndWait(m_lock, m_tag);
                clear();
        }

private:
        NTSTATUS m_acquired = STATUS_SUCCESS;
        IO_REMOVE_LOCK *m_lock{};
        void *m_tag{};
};

} // namespace libdrv
