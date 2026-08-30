/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "urb_ptr.h"

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
libdrv::urb_ptr::~urb_ptr()
{
        if (m_handle && m_urb) {
                USBD_UrbFree(m_handle, m_urb);
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::urb_ptr::alloc(_In_opt_ IO_STACK_LOCATION *stack)
{
        if (!m_handle) {
                return STATUS_INVALID_PARAMETER;
        }

        auto st = m_urb ? STATUS_ALREADY_INITIALIZED : USBD_UrbAllocate(m_handle, &m_urb); 

        if (NT_SUCCESS(st) && stack) {
                USBD_AssignUrbToIoStackLocation(m_handle, stack, m_urb);
        }

        return st;
}
