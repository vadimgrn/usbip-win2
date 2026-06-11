/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include "codeseg.h"

namespace usbip
{

struct irp_ptr_tag {};
using irp_ptr_base = generic_handle<IRP*, irp_ptr_tag, nullptr>;

template<>
inline void close_handle(_In_ irp_ptr_base::type irp, _In_ irp_ptr_base::tag_type)
{
        IoFreeIrp(irp);
}

} // namespace usbip


namespace libdrv
{

using usbip::swap;

class irp_ptr : public usbip::irp_ptr_base
{
public:
        using usbip::irp_ptr_base::irp_ptr_base;

        irp_ptr(_In_ CCHAR StackSize, _In_ bool ChargeQuota) :
                irp_ptr(IoAllocateIrp(StackSize, ChargeQuota)) {}
};


class RaiseIrql
{
public:
	_IRQL_requires_max_(HIGH_LEVEL)
	_IRQL_raises_(new_irql)
	_IRQL_saves_global_(old_irql, this)
	explicit RaiseIrql(_In_ KIRQL new_irql) { KeRaiseIrql(new_irql, &old_irql); }

	_IRQL_requires_max_(HIGH_LEVEL)
	_IRQL_restores_global_(old_irql, this)
	~RaiseIrql() { KeLowerIrql(old_irql); }

	RaiseIrql(_In_ const RaiseIrql&) = delete;
	RaiseIrql& operator =(_In_ const RaiseIrql&) = delete;

private:
	KIRQL old_irql{};
};


inline auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

inline auto get_irp(_In_ LIST_ENTRY *entry)
{
	return CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
}

/*
 * The fourth parameter is used by WSK. 
 */
template<auto N>
inline decltype(auto) argv(_In_ IRP *irp) // -> void*&
{
	NT_ASSERT(irp);
        return irp->Tail.Overlay.DriverContext[N];
}

template<typename R, auto N>
inline auto argv(_In_ IRP *irp) // pointer
{
	return static_cast<R>(argv<N>(irp));
}

template<auto N>
inline decltype(auto) argvi(_In_ IRP *irp) // -> uintptr_t&
{
        return reinterpret_cast<uintptr_t&>(argv<N>(irp));
}

template<typename R, auto N>
inline auto argvi(_In_ IRP *irp) // -> integral
{
	return static_cast<R>(argvi<N>(irp));
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

inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace libdrv

