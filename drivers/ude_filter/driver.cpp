/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 * 
 * @see https://github.com/desowin/usbpcap/tree/master/USBPcapDriver
 */

#include "driver.h"
#include "trace.h"
#include "driver.tmh"

#include "irp.h"
#include "device.h"
#include "pnp.h"

namespace
{

using namespace usbip;

_Function_class_(DRIVER_UNLOAD)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void driver_unload(_In_ DRIVER_OBJECT *drvobj)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(drvobj));
	WPP_CLEANUP(drvobj);
}

_Function_class_(DRIVER_DISPATCH)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
NTSTATUS dispatch_lower(_In_ DEVICE_OBJECT *devobj, _Inout_ IRP *irp)
{
	if (auto &lower = get_device_ext(devobj).lower) {
		return ForwardIrpAsync(lower, irp);
	}

	auto &stack = *IoGetCurrentIrpStackLocation(irp);

	auto err = STATUS_INVALID_DEVICE_REQUEST;
	Trace(TRACE_LEVEL_ERROR, "%!pnpmj!.%!pnpmn! %!STATUS!", stack.MajorFunction, stack.MinorFunction, err);

	return CompleteRequest(irp, err);
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *drvobj, _In_ UNICODE_STRING *RegistryPath)
{
	PAGED_CODE();

	WPP_INIT_TRACING(drvobj, RegistryPath);
	Trace(TRACE_LEVEL_INFORMATION, "%04x, '%!USTR!'", ptr04x(drvobj), RegistryPath);

	drvobj->DriverUnload = driver_unload;
	drvobj->DriverExtension->AddDevice = add_device;

	for (int i = 0; i < ARRAYSIZE(drvobj->MajorFunction); ++i) {
		drvobj->MajorFunction[i] = dispatch_lower;
	}

	drvobj->MajorFunction[IRP_MJ_PNP] = pnp;
	return STATUS_SUCCESS;
}
