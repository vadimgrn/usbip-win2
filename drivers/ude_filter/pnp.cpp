/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "driver.h"
#include "device.h"
#include "irp.h"

#include <libdrv\remove_lock.h>

namespace
{

using namespace usbip;

constexpr auto SizeOf_DEVICE_RELATIONS(_In_ ULONG cnt)
{
	return sizeof(DEVICE_RELATIONS) + (cnt ? --cnt*sizeof(*DEVICE_RELATIONS::Objects) : 0);
}
static_assert(SizeOf_DEVICE_RELATIONS(0) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(1) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(2)  > sizeof(DEVICE_RELATIONS));

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto clone(_In_ const DEVICE_RELATIONS &src)
{
	PAGED_CODE();

	auto sz = SizeOf_DEVICE_RELATIONS(src.Count);
	auto dst = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, sz, pooltag);

	if (dst) {
		RtlCopyMemory(dst, &src, sz);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
	}

	return dst;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto contains(_In_ const DEVICE_RELATIONS &r, _In_ const DEVICE_OBJECT *devobj)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	for (ULONG i = 0; i < r.Count; ++i) {
		if (r.Objects[i] == devobj) {
			return true;
		}
	}

	return false;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void query_bus_relations(_Inout_ DEVICE_RELATIONS* &old_rel, _In_ const DEVICE_RELATIONS &rel)
{
	PAGED_CODE();

	for (ULONG i = 0; i < rel.Count; ++i) {
		auto obj = rel.Objects[i];
		if (!(old_rel && contains(*old_rel, obj))) {
			TraceDbg("Newly added %04x", ptr04x(obj));
		}
	}

	if (old_rel) {
		ExFreePoolWithTag(old_rel, pooltag);
	}

	old_rel = rel.Count ? clone(rel) : nullptr;
}

/*
 * After we forward the request, the bus driver have created or deleted
 * a child device object. When bus driver created one (or more), this is the PDO
 * of our target device, we create and attach a filter object to it.
 * Note that we only attach the last detected USB device on it's Hub.
*/
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto query_bus_relations(_In_ filter_ext &fltr, _In_ IRP *irp)
{
	PAGED_CODE();

	auto st = ForwardIrpAndWait(fltr.lower, irp);
	TraceDbg("%!STATUS!", st);

	if (NT_SUCCESS(st)) {
		if (auto r = reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information)) {
			query_bus_relations(fltr.children, *r);
		}
	}

	return CompleteRequest(irp, st);
}

} // namespace

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
PAGED NTSTATUS usbip::pnp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();

	auto &fltr = *get_filter_ext(devobj);

	libdrv::RemoveLockGuard lck(fltr.remove_lock);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}
	
	NTSTATUS st;

	switch (auto &stack = *IoGetCurrentIrpStackLocation(irp); stack.MinorFunction) {
	case IRP_MN_REMOVE_DEVICE: // a driver must set Irp->IoStatus.Status to STATUS_SUCCESS
		st = ForwardIrpAsync(fltr.lower, irp);
		Trace(TRACE_LEVEL_INFORMATION, "REMOVE_DEVICE %!STATUS! (must be SUCCESS)", st);
		lck.release_and_wait();
		destroy(fltr);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		if (stack.Parameters.QueryDeviceRelations.Type == BusRelations) {
			st = query_bus_relations(fltr, irp);
			break;
		}
		[[fallthrough]];
	default:
		st = dispatch_lower_nolock(fltr, irp);
	}

	return st;
}
