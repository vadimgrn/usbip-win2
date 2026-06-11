/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/generic_handle_ex.h>
#include "codeseg.h"

namespace usbip
{

struct irp_ptr_tag {};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<>
inline void close_handle(_In_ IRP *irp, _In_ irp_ptr_tag)
{
        IoFreeIrp(irp);
}

} // namespace usbip


namespace libdrv
{

using usbip::swap;

class irp_ptr : public usbip::generic_handle<IRP*, usbip::irp_ptr_tag, nullptr>
{
public:
        using usbip::generic_handle<IRP*, usbip::irp_ptr_tag, nullptr>::generic_handle;

        irp_ptr(_In_ CCHAR StackSize, _In_ bool ChargeQuota) :
                irp_ptr(IoAllocateIrp(StackSize, ChargeQuota)) {}

	auto operator &() const { return get(); }
	auto operator ->() const { return get(); }
	auto& operator *() const { return *get(); }
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


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto get_irp(_In_ LIST_ENTRY *entry)
{
	return CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
}

/*
 * IRP.Tail.Overlay.DriverContext[] must not be used.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<auto N>
inline decltype(auto) argv(_In_ IRP *irp)
{
        auto loc = IoGetCurrentIrpStackLocation(irp);
        auto &p = loc->Parameters.Others;

        static_assert(N >= 0);
        static_assert(N < sizeof(p)/sizeof(p.Argument1));

        return (&p.Argument1)[N];
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<typename R, auto N>
inline decltype(auto) argv(_In_ IRP *irp)
{
        return static_cast<R>(argv<N>(irp));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<auto N>
inline decltype(auto) argvi(_In_ IRP *irp)
{
        return reinterpret_cast<uintptr_t&>(argv<N>(irp));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
template<typename R, auto N>
inline decltype(auto) argvi(_In_ IRP *irp)
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

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace libdrv

