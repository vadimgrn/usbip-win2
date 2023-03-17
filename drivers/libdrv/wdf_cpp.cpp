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

auto wdf::ObjectDelete::operator =(_Inout_ ObjectDelete&& r) -> ObjectDelete&
{
        reset(r.release());
        return *this;
}

void wdf::ObjectDelete::do_delete()
{
        if (m_obj) {
                WdfObjectDelete(m_obj);
        }
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
                do_delete();
                m_obj = obj;
        }
}

void wdf::ObjectDelete::swap(_Inout_ ObjectDelete &r)
{
        auto tmp = m_obj;
        m_obj = r.m_obj;
        r.m_obj = tmp;
}

auto wdf::Registry::operator =(_Inout_ Registry&& r) -> Registry&
{
        reset(r.release());
        return *this;
}

void wdf::Registry::do_close()
{
        if (m_key) {
                WdfRegistryClose(m_key);
        }
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
                do_close();
                m_key = key;
        }
}

void wdf::Registry::swap(_Inout_ Registry &r)
{
        auto tmp = m_key;
        m_key = r.m_key;
        r.m_key = tmp;
}
