/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdfobjectref.h"

wdf::WdfObjectRef::WdfObjectRef(WDFOBJECT handle, bool add_ref) :
        m_handle(handle) 
{
        if (m_handle && add_ref) {
                WdfObjectReference(m_handle);
        }
}

wdf::WdfObjectRef::~WdfObjectRef()
{
        if (m_handle) {
                WdfObjectDereference(m_handle);
        }
}

auto wdf::WdfObjectRef::operator =(const WdfObjectRef &obj) -> WdfObjectRef&
{
        reset(obj.m_handle);
        return *this;
}

auto wdf::WdfObjectRef::operator =(WdfObjectRef &&obj) -> WdfObjectRef&
{
        reset(obj.release(), false);
        return *this;
}

WDFOBJECT wdf::WdfObjectRef::release()
{
        auto h = m_handle;
        m_handle = WDF_NO_HANDLE;
        return h;
}

void wdf::WdfObjectRef::reset(WDFOBJECT handle, bool add_ref)
{
        if (m_handle == handle) {
                return;
        }

        if (m_handle) {
                WdfObjectDereference(m_handle);
        }

        m_handle = handle;

        if (m_handle && add_ref) {
                WdfObjectReference(m_handle);
        }
}
