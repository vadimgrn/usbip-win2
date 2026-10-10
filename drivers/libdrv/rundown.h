/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

namespace libdrv
{

struct adopt_rundown_t {};
inline constexpr adopt_rundown_t adopt_rundown;

class rundown_guard
{
public:
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        explicit rundown_guard(_Inout_ EX_RUNDOWN_REF &ref) :
                m_ref(ExAcquireRundownProtection(&ref) ? &ref : nullptr) {}

        constexpr rundown_guard(_Inout_ EX_RUNDOWN_REF &ref, _In_ adopt_rundown_t) :
                m_ref(&ref) {}

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~rundown_guard()
        {
                if (m_ref) {
                        ExReleaseRundownProtection(m_ref);
                }
        }

        rundown_guard(const rundown_guard&) = delete;
        rundown_guard& operator=(const rundown_guard&) = delete;

        constexpr bool is_acquired() const { return m_ref; }
        constexpr explicit operator bool() const { return is_acquired(); }

        constexpr auto release()
        {
                auto ptr = m_ref;
                m_ref = nullptr;
                return ptr;
        }

private:
        EX_RUNDOWN_REF *m_ref{};
};

} // namespace libdrv
