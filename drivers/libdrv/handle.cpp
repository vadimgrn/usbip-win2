/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "handle.h"

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void libdrv::handle::do_close()
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

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void libdrv::handle::reset(_In_ HANDLE h)
{
        PAGED_CODE();

        if (m_handle != h) {
                do_close();
                m_handle = h;
        }
}
