/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include "codeseg.h"

namespace usbip
{

struct irp_ptr_traits
{
        static IRP* invalid() { return nullptr; }
};

template<>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void close_handle(_In_ IRP *irp, _In_ irp_ptr_traits)
{
        IoFreeIrp(irp);
}

} // namespace usbip


namespace libdrv
{

using usbip::swap;
using usbip::generic_handle;
using usbip::irp_ptr_traits;

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
	auto operator ->() const
	{
		NT_ASSERT(get());
		return get();
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	auto& operator *() const
	{
		NT_ASSERT(get());
		return *get();
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

