/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

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
        auto get() const { return static_cast<T>(m_handle); static_assert(sizeof(T) == sizeof(m_handle)); }

        WDFOBJECT release();
        void reset(WDFOBJECT handle = WDF_NO_HANDLE, bool add_ref = true);

private:
        WDFOBJECT m_handle = WDF_NO_HANDLE;
};

} // namespace wdf
