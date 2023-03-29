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
        PAGED ~handle() { do_close(); }

        handle(_In_ const handle&) = delete;
        handle& operator =(_In_ const handle&) = delete;

        handle(_In_ handle &&h) : m_handle(h.release()) {}

        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED handle& operator =(_In_ handle&& h);

        auto get() const { return m_handle; }
        
        explicit operator bool() const { return m_handle; }
        auto operator !() const { return !m_handle; }

        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED void reset(_In_ HANDLE h = HANDLE{});

        HANDLE release()
        {
                auto h = m_handle;
                m_handle = HANDLE{};
                return h;
        }

private:
        HANDLE m_handle{};
        
        _IRQL_requires_(PASSIVE_LEVEL)
        PAGED void do_close();
};

} // namespace libdrv
