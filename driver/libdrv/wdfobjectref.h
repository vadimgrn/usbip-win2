/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

namespace usbip
{

class WdfObjectRef
{
public:
        explicit WdfObjectRef(WDFOBJECT handle = WDF_NO_HANDLE, bool add_ref = true);
        ~WdfObjectRef() { reset(); }

        WdfObjectRef(const WdfObjectRef &obj) : WdfObjectRef(obj.m_handle) {}
        WdfObjectRef& operator =(const WdfObjectRef &obj);

        WdfObjectRef(WdfObjectRef &&obj) : m_handle(obj.release()) {}
        WdfObjectRef& operator =(WdfObjectRef &&obj);

        explicit operator bool() const { return m_handle; }
        auto operator !() const { return !m_handle; }

        auto get() const { return m_handle; }

        template<typename T>
        auto get() const { return static_cast<T>(m_handle); static_assert(sizeof(T) == sizeof(m_handle)); }

        WDFOBJECT release();
        void reset(WDFOBJECT handle = WDF_NO_HANDLE);

private:
        WDFOBJECT m_handle = WDF_NO_HANDLE;
};

} // namespace usbip
