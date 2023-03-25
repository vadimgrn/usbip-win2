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


class ObjectDelete
{
public:
        ObjectDelete() = default;
        explicit ObjectDelete(_In_ WDFOBJECT obj) : m_obj(obj) {}

        ~ObjectDelete() { do_delete(); }

        ObjectDelete(const ObjectDelete&) = delete;
        ObjectDelete& operator =(const ObjectDelete&) = delete;

        ObjectDelete(_Inout_ ObjectDelete&& r) : m_obj(r.release()) {}
        ObjectDelete& operator =(_Inout_ ObjectDelete&& r);

        template<typename T>
        auto get() const { return static_cast<T>(m_obj); }

        explicit operator bool() const { return m_obj; }
        auto operator !() const { return !m_obj; }

        WDFOBJECT release();
        void reset(_In_ WDFOBJECT obj = WDF_NO_HANDLE);

        void swap(_Inout_ ObjectDelete &r);

private:
        WDFOBJECT m_obj = WDF_NO_HANDLE;
        void do_delete();
};

inline void swap(_Inout_ ObjectDelete &a, _Inout_ ObjectDelete &b)
{
        a.swap(b);
}


class Registry
{
public:
        Registry() = default;
        explicit Registry(_In_ WDFKEY key) : m_key(key) {}

        ~Registry() { do_close(); }

        Registry(const Registry&) = delete;
        Registry& operator =(const Registry&) = delete;

        Registry(_Inout_ Registry&& r) : m_key(r.release()) {}
        Registry& operator =(_Inout_ Registry&& r);

        auto operator &() { return &m_key; }
        auto get() const { return m_key; }

        explicit operator bool() const { return m_key; }
        auto operator !() const { return !m_key; }

        void close() noexcept { reset(); }
        void swap(_Inout_ Registry &r);

        WDFKEY release();
        void reset(_In_ WDFKEY key = WDF_NO_HANDLE);

private:
        WDFKEY m_key = WDF_NO_HANDLE;
        void do_close();
};


inline void swap(_Inout_ Registry &a, _Inout_ Registry &b)
{
        a.swap(b);
}

} // namespace wdf
