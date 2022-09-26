/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdfutils.h"

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
