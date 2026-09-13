/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"
#include <libusbip/generic_handle_ex.h>

#include <wdm.h>

namespace libdrv
{

struct irp_ptr_traits
{
        static IRP* invalid() { return nullptr; }
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void close_handle(_In_ IRP *irp, _In_ irp_ptr_traits)
{
        IoFreeIrp(irp);
}

using usbip::generic_handle;

class irp_ptr : public generic_handle<irp_ptr_traits>
{
public:
        using generic_handle<irp_ptr_traits>::generic_handle;

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
        irp_ptr(_In_ CCHAR StackSize, _In_ bool ChargeQuota) :
                irp_ptr(IoAllocateIrp(StackSize, ChargeQuota)) {}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr auto operator ->(this auto&& self)
	{
		NT_ASSERT(self.get());
		return self.get();
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr auto& operator *(this auto&& self)
	{
		NT_ASSERT(self.get());
		return *self.get();
	}
};


class sync_irp
{
public:
        _IRQL_requires_max_(DISPATCH_LEVEL)
        sync_irp() { ctor(); } // works for allocations on stack

        _IRQL_requires_max_(DISPATCH_LEVEL)
        ~sync_irp() { dtor(); }

        _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS ctor(_In_ CCHAR StackSize = 1, _In_ bool ChargeQuota = false);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void dtor();

        sync_irp(_In_ const sync_irp&) = delete;
        sync_irp& operator=(_In_ const sync_irp&) = delete;

        constexpr explicit operator bool(this auto&& self) { return self.m_irp; }
        constexpr auto operator !(this auto&& self) { return !self.m_irp; }

        constexpr auto get(this auto&& self) { return self.m_irp; }
        constexpr auto operator ->(this auto&& self)
        {
                NT_ASSERT(self.m_irp);
                return self.m_irp;
        }
        constexpr auto& operator *(this auto&& self) { NT_ASSERT(self.m_irp); return *self.m_irp; }

        _IRQL_requires_max_(APC_LEVEL)
        PAGED NTSTATUS wait_for_completion(_Inout_ NTSTATUS &status);

        _IRQL_requires_max_(DISPATCH_LEVEL)
        void reset();

private:
        IRP *m_irp{};
        KEVENT m_event{};

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        static NTSTATUS completion(_In_ DEVICE_OBJECT*, _In_ IRP*, _In_ void *context);

        _IRQL_requires_same_
        _IRQL_requires_max_(DISPATCH_LEVEL)
        void set_completion_routine()
        {
                NT_ASSERT(m_irp);
                IoSetCompletionRoutine(m_irp, completion, this, true, true, true);
        }
};


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

/*
 * @param entry must be Tail.Overlay.ListEntry.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto get_irp(_In_ LIST_ENTRY *entry)
{
        NT_ASSERT(entry);
        auto irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);

        NT_ASSERT(irp->Type == IO_TYPE_IRP);
        NT_ASSERT(irp->Size >= sizeof(IRP));

        return irp;
}

/*
 * IRP.Tail.Overlay.DriverContext[] must not be used.
 */
template<typename T>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_params_others(_In_ IO_STACK_LOCATION *loc)
{
        NT_ASSERT(loc);
        auto &p = loc->Parameters.Others;

        static_assert(sizeof(T) <= sizeof(p));
        static_assert(alignof(T) <= alignof(decltype(p))); 

        return reinterpret_cast<T&>(p);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace libdrv

