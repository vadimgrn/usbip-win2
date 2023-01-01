/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "irp.h"
#include "device.h"

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
PAGED NTSTATUS usbip::pnp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	
	auto &fltr = get_device_ext(devobj);
	NTSTATUS st;

	switch (auto stack = IoGetCurrentIrpStackLocation(irp); stack->MinorFunction) {
	case IRP_MN_REMOVE_DEVICE: // a driver must set Irp->IoStatus.Status to STATUS_SUCCESS
		st = ForwardIrpAsync(fltr.lower, irp);
		Trace(TRACE_LEVEL_INFORMATION, "REMOVE_DEVICE %!STATUS! (must be SUCCESS)", st);
		destroy(fltr);
		break;
//	case IRP_MN_QUERY_DEVICE_RELATIONS:
//		Trace(TRACE_LEVEL_INFORMATION, "QUERY_DEVICE_RELATIONS %!STATUS! (must be SUCCESS)", st);
//		break;
	default:
		st = dispatch_lower(devobj, irp);
	}

	return st;
}
