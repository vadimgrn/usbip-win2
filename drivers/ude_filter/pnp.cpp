/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "irp.h"
#include "device.h"

#include <libdrv\remove_lock.h>

namespace
{

using namespace usbip;

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
		auto &r = *reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information);
		for (ULONG i = 0; i < r.Count; ++i) {
			auto pdo = r.Objects[i];
			TraceDbg("#%lu pdo %04x", i, ptr04x(pdo));
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
		Trace(TRACE_LEVEL_ERROR, "%!STATUS!", err);
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
