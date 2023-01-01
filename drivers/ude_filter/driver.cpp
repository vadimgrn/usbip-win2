/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 * 
 * @see https://github.com/desowin/usbpcap/tree/master/USBPcapDriver
 */

#include "driver.h"
#include "trace.h"
#include "driver.tmh"

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
