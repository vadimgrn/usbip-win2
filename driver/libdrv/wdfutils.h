/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

namespace wdf
{

class ObjectReference
{
public:
        ObjectReference() = default;
        explicit ObjectReference(WDFOBJECT handle, bool add_ref = true);

        ~ObjectReference();

        ObjectReference(const ObjectReference &obj) : ObjectReference(obj.m_handle) {}
        ObjectReference& operator =(const ObjectReference &obj);

        ObjectReference(ObjectReference &&obj) : m_handle(obj.release()) {}
        ObjectReference& operator =(ObjectReference &&obj);

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

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void ObjectDeleteSafe(_In_ WDFOBJECT Object)
{
        if (Object) {
                WdfObjectDelete(Object);
        }
}

} // namespace wdf
