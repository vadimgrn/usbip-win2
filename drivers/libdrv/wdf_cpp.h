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


class WaitLock
{
public:
        WaitLock() = default;
                
        _IRQL_requires_max_(PASSIVE_LEVEL)
        explicit WaitLock(_In_ WDFWAITLOCK lock) : m_lock(lock) 
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

        explicit Lock(_In_ type obj) : m_lock(obj)
        { 
                acquire_lock(m_lock);
        }

        ~Lock() { release(); }

        Lock(_In_ const Lock&) = delete;
        Lock& operator =(_In_ const Lock&) = delete;

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
inline void acquire_lock(_In_ WDFSPINLOCK handle)
{
        WdfSpinLockAcquire(handle);
}

template<>
inline void release_lock(_In_ WDFSPINLOCK handle)
{
        WdfSpinLockRelease(handle);
}

/*
 * Must be declared last, WDFOBJECT is typeless.
 * WDFOBJECT -> HANDLE -> void*
 */
template<>
inline void acquire_lock(_In_ WDFOBJECT handle)
{
        WdfObjectAcquireLock(handle);
}

template<>
inline void release_lock(_In_ WDFOBJECT handle)
{
        WdfObjectReleaseLock(handle);
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
