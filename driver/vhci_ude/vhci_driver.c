#include "vhci_driver.h"
#include "vhci_driver.tmh"
#include "vhci_hc.h"

static PAGEABLE VOID
cleanup_vhci(_In_ WDFOBJECT drvobj)
{
	PAGED_CODE();

	TRD(DRIVER, "Enter");

	WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)drvobj));
}

static PAGEABLE VOID
driver_unload(_In_ WDFDRIVER drvobj)
{
	PAGED_CODE();
	TRD(DRIVER, "Enter");

	WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)drvobj));
}

DRIVER_INITIALIZE DriverEntry;

INITABLE NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT drvobj, _In_ PUNICODE_STRING regpath)
{
	WDF_DRIVER_CONFIG	conf;
	NTSTATUS		status;
	WDF_OBJECT_ATTRIBUTES	attrs;

	PAGED_CODE();
	WPP_INIT_TRACING(drvobj, regpath);

	TRD(DRIVER, "Enter");

	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.EvtCleanupCallback = cleanup_vhci;

	WDF_DRIVER_CONFIG_INIT(&conf, evt_add_vhci);
	conf.DriverPoolTag = VHCI_POOLTAG;
	conf.EvtDriverUnload = driver_unload;

	status = WdfDriverCreate(drvobj, regpath, &attrs, &conf, WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		TRE(DRIVER, "WdfDriverCreate failed: %!STATUS!", status);
		WPP_CLEANUP(drvobj);
		return status;
	}

	TRD(DRIVER, "Leave: %!STATUS!", status);

	return status;
}
