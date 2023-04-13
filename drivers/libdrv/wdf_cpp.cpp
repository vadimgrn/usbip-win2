/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

wdf::ObjectDelete::~ObjectDelete()
{
        if (m_obj) {
                WdfObjectDelete(m_obj);
        }
}

auto wdf::ObjectDelete::operator =(_Inout_ ObjectDelete&& r) -> ObjectDelete&
{
        reset(r.release());
        return *this;
}

WDFOBJECT wdf::ObjectDelete::release()
{
        auto obj = m_obj;
        m_obj = WDF_NO_HANDLE;
        return obj;
}

void wdf::ObjectDelete::reset(_In_ WDFOBJECT obj)
{
        if (m_obj != obj) {
                ObjectDelete(obj).swap(*this);
        }
}

void wdf::ObjectDelete::swap(_Inout_ ObjectDelete &r)
{
        auto tmp = m_obj;
        m_obj = r.m_obj;
        r.m_obj = tmp;
}

wdf::Registry::~Registry()
{
        if (m_key) {
                WdfRegistryClose(m_key);
        }
}

auto wdf::Registry::operator =(_Inout_ Registry&& r) -> Registry&
{
        reset(r.release());
        return *this;
}

WDFKEY wdf::Registry::release()
{
        auto key = m_key;
        m_key = WDF_NO_HANDLE;
        return key;
}

void wdf::Registry::reset(_In_ WDFKEY key)
{
        if (m_key != key) {
                Registry(key).swap(*this);
        }
}

void wdf::Registry::swap(_Inout_ Registry &r)
{
        auto tmp = m_key;
        m_key = r.m_key;
        r.m_key = tmp;
}
