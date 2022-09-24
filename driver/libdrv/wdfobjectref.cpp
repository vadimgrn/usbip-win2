/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdfobjectref.h"

usbip::WdfObjectRef::WdfObjectRef(WDFOBJECT handle, bool add_ref) :
        m_handle(handle) 
{
        if (m_handle && add_ref) {
                WdfObjectReference(m_handle);
        }
}

auto usbip::WdfObjectRef::operator =(const WdfObjectRef &obj) -> WdfObjectRef&
{
        reset(obj.m_handle);
        return *this;
}

auto usbip::WdfObjectRef::operator =(WdfObjectRef &&obj) -> WdfObjectRef&
{
        reset(obj.release());
        return *this;
}

WDFOBJECT usbip::WdfObjectRef::release()
{
        auto h = m_handle;
        m_handle = WDF_NO_HANDLE;
        return h;
}

void usbip::WdfObjectRef::reset(WDFOBJECT handle)
{
        if (m_handle == handle) {
                return;
        }

        if (m_handle) {
                WdfObjectDereference(m_handle);
        }

        m_handle = handle;

        if (m_handle) {
                WdfObjectReference(m_handle);
        }
}
