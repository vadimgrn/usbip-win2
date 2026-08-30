/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

#include "codeseg.h"
#include <libusbip/generic_handle_ex.h>

#include <wdf.h>

namespace wdf
{

class ObjectRef
{
public:
        constexpr ObjectRef() = default;
        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        explicit ObjectRef(_In_opt_ WDFOBJECT handle, _In_ bool add_ref = true);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~ObjectRef();

        ObjectRef(const ObjectRef &obj) : ObjectRef(obj.m_handle) {}
        ObjectRef& operator =(const ObjectRef &obj);

        ObjectRef(ObjectRef &&obj) : m_handle(obj.release()) {}
        ObjectRef& operator =(ObjectRef &&obj);

        constexpr explicit operator bool() const { return m_handle; }
        constexpr auto operator !() const { return !m_handle; }

        constexpr bool operator ==(decltype(nullptr)) const { return m_handle == WDF_NO_HANDLE; }
        constexpr bool operator !=(decltype(nullptr)) const { return m_handle != WDF_NO_HANDLE; }

        friend constexpr bool operator ==(const ObjectRef &a, const ObjectRef &b) { return a.m_handle == b.m_handle; }
        friend constexpr bool operator !=(const ObjectRef &a, const ObjectRef &b) { return a.m_handle != b.m_handle; }

        auto get() const { return m_handle; }

        template<typename T>
        auto get() const { return static_cast<T>(m_handle); }

        WDFOBJECT release();

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset(_In_opt_ WDFOBJECT handle = WDF_NO_HANDLE, _In_ bool add_ref = true);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void swap(_Inout_ ObjectRef &r);

private:
        WDFOBJECT m_handle = WDF_NO_HANDLE;
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void swap(_Inout_ ObjectRef &a, _Inout_ ObjectRef &b)
{
        a.swap(b);
}


class WaitLock
{
public:
        constexpr WaitLock() = default;
                
        _IRQL_requires_max_(PASSIVE_LEVEL)
        PAGED explicit WaitLock(_In_ WDFWAITLOCK lock) : m_lock(lock) 
        { 
                PAGED_CODE();
                WdfWaitLockAcquire(m_lock, nullptr); 
        }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~WaitLock() { release(); }

        WaitLock(_In_ const WaitLock&) = delete;
        WaitLock& operator =(_In_ const WaitLock&) = delete;

        _When_(timeout == NULL, _IRQL_requires_max_(PASSIVE_LEVEL))
        _When_(timeout != NULL && *timeout == 0, _IRQL_requires_max_(DISPATCH_LEVEL))
        _When_(timeout != NULL && *timeout != 0, _IRQL_requires_max_(PASSIVE_LEVEL))
        _When_(timeout != NULL, _Must_inspect_result_)
        NTSTATUS acquire(_In_ WDFWAITLOCK lock, _In_opt_ LONGLONG *timeout = nullptr);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void release()
        {
                if (m_lock) {
                        WdfWaitLockRelease(m_lock);
                        m_lock = WDF_NO_HANDLE;
                }
        }

private:
        WDFWAITLOCK m_lock = WDF_NO_HANDLE;
};


/*
 * Full specialization of these functions must be defined for each used type.
 */
template<typename T>
void acquire_lock(_In_ T);

template<typename T>
void release_lock(_In_ T);


template<typename T>
class Lock
{
public:
        using type = T;

        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_raises_(DISPATCH_LEVEL)
        _IRQL_saves_global_(m_lock, this)
        explicit Lock(_In_ type obj) : m_lock(obj)
        { 
                acquire_lock(m_lock);
        }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_requires_min_(DISPATCH_LEVEL)
        _IRQL_restores_global_(m_lock, this)
        ~Lock() { release(); }

        Lock(_In_ const Lock&) = delete;
        Lock& operator =(_In_ const Lock&) = delete;

        _IRQL_requires_max_(DISPATCH_LEVEL)
        _IRQL_requires_min_(DISPATCH_LEVEL)
        _IRQL_restores_global_(m_lock, this)
        void release()
        {
                if (auto handle = (type)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&m_lock), WDF_NO_HANDLE)) {
                        release_lock(handle);
                }
        }

private:
        type m_lock = WDF_NO_HANDLE;
};


template<>
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
inline void acquire_lock(
        _In_ 
        _Requires_lock_not_held_(_Curr_)
        _Acquires_lock_(_Curr_)
        _IRQL_saves_
        WDFSPINLOCK handle)
{
        WdfSpinLockAcquire(handle);
}

template<>
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
inline void release_lock(
        _In_ 
        _Requires_lock_held_(_Curr_)
        _Releases_lock_(_Curr_)
        _IRQL_restores_
        WDFSPINLOCK handle)
{
        WdfSpinLockRelease(handle);
}

/*
 * Must be declared last, WDFOBJECT is typeless.
 * WDFOBJECT -> HANDLE -> void*
 */
template<>
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void acquire_lock(_In_ WDFOBJECT handle)
{
        WdfObjectAcquireLock(handle);
}

template<>
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void release_lock(_In_ WDFOBJECT handle)
{
        WdfObjectReleaseLock(handle);
}


struct wdfobject_traits
{
        static WDFOBJECT invalid() { return WDF_NO_HANDLE; }
};

struct wdfkey_traits
{
        static WDFKEY invalid() { return WDF_NO_HANDLE; }
};

using ObjectDelete = usbip::generic_handle<wdfobject_traits>;
using Registry = usbip::generic_handle<wdfkey_traits>;

using usbip::swap;

} // namespace wdf


namespace usbip
{

using wdf::ObjectDelete;

template<>
inline void close_handle(_In_ ObjectDelete::type obj, _In_ ObjectDelete::tag_type)
{
        WdfObjectDelete(obj);
}

using wdf::Registry;

template<>
inline void close_handle(_In_ Registry::type key, _In_ Registry::tag_type)
{
        WdfRegistryClose(key);
}

} // namespace usbip
