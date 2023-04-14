/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

/*
 * warning C4471: '_WDF_REQUEST_TYPE': a forward declaration of an unscoped enumeration 
 * must have an underlying type.
 * P.S. Set C++ "All Options"/AdditionalOptions: /Zc:__cplusplus
 */
#if __cplusplus > 201703L
  enum _WDF_REQUEST_TYPE : int;
#endif

#include <libusbip/generic_handle_ex.h>

#include <wdm.h>
#include <wdf.h>

namespace wdf
{

class ObjectRef
{
public:
        ObjectRef() = default;
        explicit ObjectRef(WDFOBJECT handle, bool add_ref = true);

        ~ObjectRef();

        ObjectRef(const ObjectRef &obj) : ObjectRef(obj.m_handle) {}
        ObjectRef& operator =(const ObjectRef &obj);

        ObjectRef(ObjectRef &&obj) : m_handle(obj.release()) {}
        ObjectRef& operator =(ObjectRef &&obj);

        explicit operator bool() const { return m_handle; }
        auto operator !() const { return !m_handle; }

        auto get() const { return m_handle; }

        template<typename T>
        auto get() const { return static_cast<T>(m_handle); }

        WDFOBJECT release();
        void reset(WDFOBJECT handle = WDF_NO_HANDLE, bool add_ref = true);

        void swap(_Inout_ ObjectRef &r);

private:
        WDFOBJECT m_handle = WDF_NO_HANDLE;
};


inline void swap(_Inout_ ObjectRef &a, _Inout_ ObjectRef &b)
{
        a.swap(b);
}


using usbip::generic_handle;

struct ObjectDeleteTag {};
using ObjectDelete = generic_handle<WDFOBJECT, ObjectDeleteTag, WDFOBJECT(WDF_NO_HANDLE)>;

struct RegistryTag {};
using Registry = generic_handle<WDFKEY, RegistryTag, WDFKEY(WDF_NO_HANDLE)>;

using usbip::swap;

} // namespace wdf


namespace usbip
{

using wdf::ObjectDelete;

template<>
inline void close_handle(_In_ ObjectDelete::type obj, _In_ ObjectDelete::tag_type) noexcept
{
        WdfObjectDelete(obj);
}

using wdf::Registry;

template<>
inline void close_handle(_In_ Registry::type key, _In_ Registry::tag_type) noexcept
{
        WdfRegistryClose(key);
}

} // namespace usbip
