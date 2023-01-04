/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
PAGED auto contains(_In_ const DEVICE_RELATIONS &r, _In_ const DEVICE_OBJECT *obj)
{
	PAGED_CODE();
	NT_ASSERT(obj);

	for (ULONG i = 0; i < r.Count; ++i) {
		if (r.Objects[i] == obj) {
			return true;
		}
	}

	return false;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void query_bus_relations(_Inout_ filter_ext &f, _In_ const DEVICE_RELATIONS &r)
{
	PAGED_CODE();

	NT_ASSERT(f.is_hub);
	auto &previous = f.hub.previous;

	for (ULONG i = 0; i < r.Count; ++i) {
		auto pdo = r.Objects[i];
		if (!(previous && contains(*previous, pdo))) {
			TraceDbg("Creating a FiDO for PDO %04x", ptr04x(pdo));
			do_add_device(f.self->DriverObject, pdo, &f);
		}
	}

	if (auto ptr = previous) {
		ExFreePoolWithTag(ptr, pooltag);
	}

	previous = r.Count ? clone(r) : nullptr;
}

/*
 * After we forward the request, the bus driver have created or deleted
 * a child device object. When bus driver created one (or more), this is the PDO
 * of our target device, we create and attach a filter object to it.
 * Note that we only attach the last detected USB device on it's Hub.
*/
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto query_bus_relations(_Inout_ filter_ext &f, _In_ IRP *irp)
{
	PAGED_CODE();

	auto st = ForwardIrpAndWait(f.lower, irp);
	TraceDbg("%!STATUS!", st);

	if (NT_SUCCESS(st)) {
		if (auto r = reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information)) {
			query_bus_relations(f, *r);
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
	case IRP_MN_REMOVE_DEVICE:
		st = ForwardIrpAsync(fltr.lower, irp);
		TraceDbg("REMOVE_DEVICE %04x, %!STATUS!", ptr04x(devobj), st);
		lck.release_and_wait();
		destroy(fltr);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		if (fltr.is_hub && stack.Parameters.QueryDeviceRelations.Type == BusRelations) {
			st = query_bus_relations(fltr, irp);
			break;
		}
		[[fallthrough]];
	default:
		st = ForwardIrpAsync(fltr.lower, irp);
	}

	return st;
}
