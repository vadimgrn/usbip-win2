#include "vhci_driver.h"
#include "vhci_driver.tmh"
#include "vhci_hc.h"

static PAGEABLE VOID
driver_unload(_In_ WDFDRIVER drvobj)
{
	PAGED_CODE();
	TraceInfo(TRACE_DRIVER, "Enter\n");
	WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)drvobj));
}

DRIVER_INITIALIZE DriverEntry;

INITABLE NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT drvobj, _In_ PUNICODE_STRING regpath)
{
	PAGED_CODE();

	WPP_INIT_TRACING(drvobj, regpath);
	TraceInfo(TRACE_DRIVER, "Enter\n");

	WDF_DRIVER_CONFIG conf;
	WDF_DRIVER_CONFIG_INIT(&conf, evt_add_vhci);

	conf.DriverPoolTag = VHCI_POOLTAG;
	conf.EvtDriverUnload = driver_unload;

	NTSTATUS status = WdfDriverCreate(drvobj, regpath, WDF_NO_OBJECT_ATTRIBUTES, &conf, WDF_NO_HANDLE);

	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_DRIVER, "%!STATUS!\n", status);
		WPP_CLEANUP(drvobj);
	}

	return status;
}
