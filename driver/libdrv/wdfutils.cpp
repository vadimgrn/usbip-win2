/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdfutils.h"

wdf::ObjectReference::ObjectReference(WDFOBJECT handle, bool add_ref) :
        m_handle(handle) 
{
        if (m_handle && add_ref) {
                WdfObjectReference(m_handle);
        }
}

wdf::ObjectReference::~ObjectReference()
{
        if (m_handle) {
                WdfObjectDereference(m_handle);
        }
}

auto wdf::ObjectReference::operator =(const ObjectReference &obj) -> ObjectReference&
{
        reset(obj.m_handle);
        return *this;
}

auto wdf::ObjectReference::operator =(ObjectReference &&obj) -> ObjectReference&
{
        reset(obj.release(), false);
        return *this;
}

WDFOBJECT wdf::ObjectReference::release()
{
        auto h = m_handle;
        m_handle = WDF_NO_HANDLE;
        return h;
}

void wdf::ObjectReference::reset(WDFOBJECT handle, bool add_ref)
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
