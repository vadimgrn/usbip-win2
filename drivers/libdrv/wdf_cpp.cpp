/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdf_cpp.h"

wdf::ObjectRef::ObjectRef(WDFOBJECT handle, bool add_ref) :
        m_handle(handle)
{
        if (m_handle && add_ref) {
                WdfObjectReference(m_handle);
        }
}

wdf::ObjectRef::~ObjectRef()
{
        if (m_handle) {
                WdfObjectDereference(m_handle);
        }
}

auto wdf::ObjectRef::operator =(const ObjectRef &obj) -> ObjectRef&
{
        reset(obj.m_handle);
        return *this;
}

auto wdf::ObjectRef::operator =(ObjectRef &&obj) -> ObjectRef&
{
        reset(obj.release(), false);
        return *this;
}

WDFOBJECT wdf::ObjectRef::release()
{
        auto h = m_handle;
        m_handle = WDF_NO_HANDLE;
        return h;
}

void wdf::ObjectRef::reset(WDFOBJECT handle, bool add_ref)
{
        if (m_handle != handle) {
                ObjectRef(handle, add_ref).swap(*this);
        }
}

void wdf::ObjectRef::swap(_Inout_ ObjectRef &r)
{
        auto tmp = r.m_handle;
        r.m_handle = m_handle;
        m_handle = tmp;
}

_When_(timeout == NULL, _IRQL_requires_max_(PASSIVE_LEVEL))
_When_(timeout != NULL && *timeout == 0, _IRQL_requires_max_(DISPATCH_LEVEL))
_When_(timeout != NULL && *timeout != 0, _IRQL_requires_max_(PASSIVE_LEVEL))
_When_(timeout != NULL, _Must_inspect_result_)
NTSTATUS wdf::WaitLock::acquire(_In_ WDFWAITLOCK lock, _In_opt_ LONGLONG *timeout)
{
        auto ret = m_lock ? STATUS_ALREADY_INITIALIZED : WdfWaitLockAcquire(lock, timeout); 
        if (NT_SUCCESS(ret)) {
                m_lock = lock;
        }
        return ret;
}
