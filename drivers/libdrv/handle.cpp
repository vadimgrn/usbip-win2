/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "handle.h"

_IRQL_requires_(PASSIVE_LEVEL)
PAGED libdrv::handle::~handle()
{
        PAGED_CODE();
        if (m_handle) {
                NT_VERIFY(NT_SUCCESS(ZwClose(m_handle)));
        }
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto libdrv::handle::operator =(_In_ handle&& h) -> handle&
{
        PAGED_CODE();
        reset(h.release());
        return *this;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
HANDLE libdrv::handle::release()
{
        auto h = m_handle;
        m_handle = HANDLE{};
        return h;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void libdrv::handle::reset(_In_ HANDLE h)
{
        PAGED_CODE();
        if (m_handle != h) {
                handle(h).swap(*this);
        }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::handle::swap(_Inout_ handle &h)
{
        auto tmp = h.m_handle;
        h.m_handle = m_handle;
        m_handle = tmp;
}
