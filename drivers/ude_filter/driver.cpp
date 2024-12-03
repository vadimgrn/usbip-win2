/*
 * Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 * 
 * @see https://github.com/desowin/usbpcap/tree/master/USBPcapDriver
 */

#include "driver.h"
#include "trace.h"
#include "driver.tmh"

#include "irp.h"
#include "pnp.h"
#include "int_dev_ctrl.h"

#include <libdrv\remove_lock.h>

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
auto dispatch_lower(_In_ DEVICE_OBJECT *devobj, _Inout_ IRP *irp)
{
	auto &f = *get_filter_ext(devobj);

	libdrv::RemoveLockGuard lck(f.remove_lock);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

	return ForwardIrp(f, irp);
}

} // namespace


_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
CS_INIT EXTERN_C NTSTATUS DriverEntry(_In_ DRIVER_OBJECT *drvobj, _In_ UNICODE_STRING *RegistryPath)
{
	ExInitializeDriverRuntime(0); // @see ExAllocatePool2

	WPP_INIT_TRACING(drvobj, RegistryPath);
	Trace(TRACE_LEVEL_INFORMATION, "%04x, '%!USTR!'", ptr04x(drvobj), RegistryPath);

	drvobj->DriverUnload = driver_unload;
	drvobj->DriverExtension->AddDevice = add_device;

	for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		drvobj->MajorFunction[i] = dispatch_lower;
	}

	drvobj->MajorFunction[IRP_MJ_PNP] = pnp;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = int_dev_ctrl;

	return STATUS_SUCCESS;
}
