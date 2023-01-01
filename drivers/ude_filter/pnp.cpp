/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "irp.h"
//#include "device.h"

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
PAGED NTSTATUS usbip::pnp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();

	auto stack = IoGetCurrentIrpStackLocation(irp);
	TraceDbg("%!pnpmn!", stack->MinorFunction);

	return ForwardIrpAsync(devobj, irp);
}
