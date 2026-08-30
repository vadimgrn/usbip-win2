/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        RemoveLockGuard(_Inout_ IO_REMOVE_LOCK &lock, _In_opt_ void *tag = nullptr) : 
                m_acquired(IoAcquireRemoveLock(&lock, tag)),
                m_lock(NT_SUCCESS(m_acquired) ? &lock : nullptr),
                m_tag(tag) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        RemoveLockGuard(_Inout_ IO_REMOVE_LOCK &lock, _In_ adopt_lock_t, _In_opt_ void *tag = nullptr) : 
                m_lock(&lock), m_tag(tag) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
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

        auto clear() 
        { 
                m_acquired = STATUS_INVALID_ADDRESS;
                m_lock = nullptr; 

                auto tag = m_tag;
                m_tag = nullptr;

                return tag;
        }

        _IRQL_requires_same_
        _IRQL_requires_max_(PASSIVE_LEVEL)
        void release_and_wait()
        {
                if (m_lock) [[likely]] {
                        IoReleaseRemoveLockAndWait(m_lock, m_tag);
                        clear();
                } else {
                        NT_ASSERT(!"release_and_wait() called without a held lock");
                }
        }

private:
        NTSTATUS m_acquired = STATUS_SUCCESS;
        IO_REMOVE_LOCK *m_lock{};
        void *m_tag{};
};

} // namespace libdrv
