/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace libdrv
{

struct adopt_lock_t {};
inline constexpr adopt_lock_t adopt_lock;

class remove_lock_guard
{
public:
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        remove_lock_guard(_Inout_ IO_REMOVE_LOCK &lock, _In_opt_ void *tag = nullptr) : 
                m_status(IoAcquireRemoveLock(&lock, tag)),
                m_lock(NT_SUCCESS(m_status) ? &lock : nullptr),
                m_tag(tag) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        remove_lock_guard(_Inout_ IO_REMOVE_LOCK &lock, _In_ adopt_lock_t, _In_opt_ void *tag = nullptr) : 
                m_lock(&lock), m_tag(tag) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~remove_lock_guard() 
        {
                if (m_lock) {
                        IoReleaseRemoveLock(m_lock, m_tag);
                }
        }

        remove_lock_guard(const remove_lock_guard&) = delete;
        remove_lock_guard& operator =(const remove_lock_guard&) = delete;

        constexpr bool is_acquired() const { return m_lock; }
        constexpr explicit operator bool() const { return is_acquired(); }

        auto status() const { return m_status; }
        auto tag() const { return m_tag; }

        auto clear() 
        { 
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
        NTSTATUS m_status = STATUS_SUCCESS;
        IO_REMOVE_LOCK *m_lock{};
        void *m_tag{};
};

} // namespace libdrv
