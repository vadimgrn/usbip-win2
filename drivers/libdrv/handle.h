/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

class handle
{
public:
        handle() = default;
        explicit handle(_In_ HANDLE h) : m_handle(h) {}

        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED ~handle();

        handle(_In_ const handle&) = delete;
        handle& operator =(_In_ const handle&) = delete;

        _IRQL_requires_max_(DISPATCH_LEVEL)
        handle(_In_ handle &&h) : m_handle(h.release()) {}

        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED handle& operator =(_In_ handle&& h);

        auto get() const { return m_handle; }
        
        explicit operator bool() const { return m_handle; }
        auto operator !() const { return !m_handle; }

        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED void reset(_In_ HANDLE h = HANDLE{});

        void close() noexcept { reset(); }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        HANDLE release();

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void swap(_Inout_ handle &h);

private:
        HANDLE m_handle{};
};


_IRQL_requires_max_(DISPATCH_LEVEL)
inline void swap(_Inout_ handle &a, _Inout_ handle &b)
{
        a.swap(b);
}

} // namespace libdrv
