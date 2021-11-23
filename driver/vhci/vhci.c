#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include "vhci_plugin.h"
#include "globals.h"
#include "usbreq.h"
#include "vhci_pnp.h"

#include <usbdi.h>

//
// Global Debug Level
//

GLOBALS Globals;

NPAGED_LOOKASIDE_LIST g_lookaside;

PAGEABLE __drv_dispatchType(IRP_MJ_READ)
DRIVER_DISPATCH vhci_read;

PAGEABLE __drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH vhci_write;

PAGEABLE __drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH vhci_ioctl;

PAGEABLE __drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH vhci_internal_ioctl;

PAGEABLE __drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH vhci_pnp;

__drv_dispatchType(IRP_MJ_POWER)
DRIVER_DISPATCH vhci_power;

PAGEABLE __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
DRIVER_DISPATCH vhci_system_control;

PAGEABLE DRIVER_ADD_DEVICE vhci_add_device;

static PAGEABLE VOID
vhci_driverUnload(__in PDRIVER_OBJECT drvobj)
{
	PAGED_CODE();
	TraceInfo(TRACE_GENERAL, "Enter");

	ExDeleteNPagedLookasideList(&g_lookaside);
	ASSERT(!drvobj->DeviceObject);

	if (Globals.RegistryPath.Buffer) {
		ExFreePool(Globals.RegistryPath.Buffer);
		Globals.RegistryPath.Buffer = NULL;
	}

	WPP_CLEANUP(drvobj);
}

static PAGEABLE NTSTATUS
vhci_create(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);

	PAGED_CODE();

	TraceInfo(TRACE_GENERAL, "%!vdev_type_t!: Enter", vdev->type);

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		TraceWarning(TRACE_GENERAL, "vhci_create(%!vdev_type_t!): no such device", vdev->type);

		Irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_NO_SUCH_DEVICE;
	}

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	TraceInfo(TRACE_GENERAL, "%!vdev_type_t!: Leave", vdev->type);

	return STATUS_SUCCESS;
}

static PAGEABLE void
cleanup_vpdo(pvhci_dev_t vhci, PIRP irp)
{
	PIO_STACK_LOCATION  irpstack;
	pvpdo_dev_t	vpdo;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	vpdo = irpstack->FileObject->FsContext;
	if (vpdo) {
		vpdo->fo = NULL;
		irpstack->FileObject->FsContext = NULL;
		if (vpdo->plugged)
			vhci_unplug_port(vhci, (CHAR)vpdo->port);
	}
}

static PAGEABLE NTSTATUS
vhci_cleanup(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	vdev_t *vdev = DEVOBJ_TO_VDEV(devobj);

	PAGED_CODE();

	TraceInfo(TRACE_GENERAL, "%!vdev_type_t!: Enter", vdev->type);

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		TraceWarning(TRACE_GENERAL, "vhci_cleanup(%!vdev_type_t!): no such device", vdev->type);
		irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_NO_SUCH_DEVICE;
	}
	if (vdev->type == VDEV_VHCI) {
		cleanup_vpdo(DEVOBJ_TO_VHCI(devobj), irp);
	}

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	TraceInfo(TRACE_GENERAL, "%!vdev_type_t!: Leave", vdev->type);

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
vhci_close(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	NTSTATUS	status;

	PAGED_CODE();

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
DriverEntry(__in PDRIVER_OBJECT drvobj, __in PUNICODE_STRING RegistryPath)
{
	WPP_INIT_TRACING(drvobj, RegistryPath);
	TraceInfo(TRACE_GENERAL, "RegistryPath '%!USTR!'", RegistryPath);

	ExInitializeNPagedLookasideList(&g_lookaside, NULL,NULL, 0, sizeof(struct urb_req), 'USBV', 0);

	// Save the RegistryPath for WMI
	Globals.RegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	Globals.RegistryPath.Length = RegistryPath->Length;
	Globals.RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Globals.RegistryPath.MaximumLength, USBIP_VHCI_POOL_TAG);

	if (Globals.RegistryPath.Buffer) {
		RtlCopyUnicodeString(&Globals.RegistryPath, RegistryPath);
	} else {
		TraceCritical(TRACE_GENERAL, "ExAllocatePoolWithTag failed");
		vhci_driverUnload(drvobj);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	drvobj->MajorFunction[IRP_MJ_CREATE] = vhci_create;
	drvobj->MajorFunction[IRP_MJ_CLEANUP] = vhci_cleanup;
	drvobj->MajorFunction[IRP_MJ_CLOSE] = vhci_close;
	drvobj->MajorFunction[IRP_MJ_READ] = vhci_read;
	drvobj->MajorFunction[IRP_MJ_WRITE] = vhci_write;
	drvobj->MajorFunction[IRP_MJ_PNP] = vhci_pnp;
	drvobj->MajorFunction[IRP_MJ_POWER] = vhci_power;
	drvobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = vhci_ioctl;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vhci_internal_ioctl;
	drvobj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = vhci_system_control;
	drvobj->DriverUnload = vhci_driverUnload;
	drvobj->DriverExtension->AddDevice = vhci_add_device;

	return STATUS_SUCCESS;
}
