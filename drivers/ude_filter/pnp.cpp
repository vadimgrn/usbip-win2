/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "driver.h"
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

	auto assign = [] (auto &ptr, auto addr) 
	{
		if (ptr) {
			ExFreePoolWithTag(ptr, pooltag);
		}
		ptr = addr;
	};

	if (!r.Count) {
		assign(previous, nullptr);
	} else if (auto ptr = clone(r)) { // leave as is in case of an error
		assign(previous, ptr);
	}
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

	auto st = ForwardIrpAndWait(f, irp);
	if (NT_SUCCESS(st)) {
		if (auto r = reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information)) {
			query_bus_relations(f, *r);
		}
	}

	CompleteRequest(irp);
	return st;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto remove_device(_In_ IRP *irp, _In_ libdrv::RemoveLockGuard &lock, _Inout_ filter_ext &fltr)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(fltr.self));

	if (auto &handle = fltr.dev.usbd) {
		USBD_CloseHandle(handle); // must be called before sending the IRP down the USB driver stack
		handle = nullptr;
	}

	auto st = ForwardIrp(fltr, irp); // drivers must not fail this IRP

	lock.release_and_wait();
	destroy(fltr);

	return st;
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
	case IRP_MN_START_DEVICE:
		st = ForwardIrpAndWait(fltr, irp); // must be started after lower device objects
		CompleteRequest(irp);
		break;
	case IRP_MN_REMOVE_DEVICE:
		st = remove_device(irp, lck, fltr);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		if (fltr.is_hub && stack.Parameters.QueryDeviceRelations.Type == BusRelations) {
			st = query_bus_relations(fltr, irp);
			break;
		}
		[[fallthrough]];
	default:
		st = ForwardIrp(fltr, irp);
	}

	return st;
}
