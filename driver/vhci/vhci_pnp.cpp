#include "vhci_pnp.h"
#include "trace.h"
#include "vhci_pnp.tmh"

#include "vhci.h"
#include "vhci_pnp_id.h"
#include "vhci_irp.h"
#include "vhci_pnp_devtext.h"
#include "vhci_pnp_intf.h"
#include "vhci_pnp_relations.h"
#include "vhci_pnp_cap.h"
#include "vhci_pnp_start.h"
#include "vhci_pnp_remove.h"
#include "vhci_pnp_resources.h"

#include <wdmguid.h>

namespace
{

PAGEABLE auto irp_pass_down_or_success(vdev_t *vdev, IRP *irp)
{
	return is_fdo(vdev->type) ? irp_pass_down(vdev->devobj_lower, irp) : irp_done_success(irp);
}

PAGEABLE NTSTATUS pnp_query_stop_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();
	SET_NEW_PNP_STATE(vdev, StopPending);
	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_cancel_stop_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	if (vdev->DevicePnPState == StopPending) {
		// We did receive a query-stop, so restore.
		RESTORE_PREVIOUS_PNP_STATE(vdev);
		NT_ASSERT(vdev->DevicePnPState == Started);
	}

	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_stop_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();
	SET_NEW_PNP_STATE(vdev, Stopped);
	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_query_remove_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	switch (vdev->type) {
	case VDEV_VPDO:
		/* vpdo cannot be removed */
		vhub_mark_unplugged_vpdo(vhub_from_vpdo((vpdo_dev_t *)vdev), (vpdo_dev_t *)vdev);
		break;
	}

	SET_NEW_PNP_STATE(vdev, RemovePending);
	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_cancel_remove_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	if (vdev->DevicePnPState == RemovePending) {
		RESTORE_PREVIOUS_PNP_STATE(vdev);
	}

	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_surprise_removal(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();
	SET_NEW_PNP_STATE(vdev, SurpriseRemovePending);
	return irp_pass_down_or_success(vdev, irp);
}

PAGEABLE NTSTATUS pnp_query_bus_information(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);

	PNP_BUS_INFORMATION *bi = (PNP_BUS_INFORMATION*)ExAllocatePoolWithTag(PagedPool, sizeof(*bi), USBIP_VHCI_POOL_TAG);
	if (bi) {
		bi->BusTypeGuid = GUID_BUS_TYPE_USB;
		bi->LegacyBusType = PNPBus;
		bi->BusNumber = 10; // arbitrary
	}

	irp->IoStatus.Information = (ULONG_PTR)bi;

	NTSTATUS st = bi ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
	return irp_done(irp, st);
}

PAGEABLE NTSTATUS pnp_0x0E(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_iostatus(irp);
}

PAGEABLE NTSTATUS pnp_read_config(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_iostatus(irp);
}

PAGEABLE NTSTATUS pnp_write_config(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_iostatus(irp);
}

/*
* For the device to be ejected, the device must be in the D3
* device power state (off) and must be unlocked
* (if the device supports locking). Any driver that returns success
* for this IRP must wait until the device has been ejected before
* completing the IRP.
*/
PAGEABLE NTSTATUS pnp_eject(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	if (vdev->type == VDEV_VPDO) {
		vpdo_dev_t* vpdo = (vpdo_dev_t*)vdev;
		vhub_mark_unplugged_vpdo(vhub_from_vpdo(vpdo), vpdo);
		return irp_done_success(irp);
	}

	return irp_done_iostatus(irp);
}

PAGEABLE NTSTATUS pnp_set_lock(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_iostatus(irp);
}

PAGEABLE NTSTATUS pnp_query_pnp_device_state(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);

	irp->IoStatus.Information = 0;
	return irp_done_success(irp);
}

/*
* OPTIONAL for bus drivers.
* This bus drivers any of the bus's descendants
* (child device, child of a child device, etc.) do not
* contain a memory file namely paging file, dump file,
* or hibernation file. So we  fail this Irp.
*/
PAGEABLE NTSTATUS pnp_device_usage_notification(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done(irp, STATUS_UNSUCCESSFUL);
}

PAGEABLE NTSTATUS pnp_query_legacy_bus_information(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_iostatus(irp);
}

/*
* This request notifies bus drivers that a device object exists and
* that it has been fully enumerated by the plug and play manager.
*/
PAGEABLE NTSTATUS pnp_device_enumerated(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vdev);
	return irp_done_success(irp);
}

typedef NTSTATUS (*pnpmn_func_t)(vdev_t*, IRP*);

const pnpmn_func_t pnpmn_functions[] =
{
	pnp_start_device, // IRP_MN_START_DEVICE
	pnp_query_remove_device,
	pnp_remove_device,
	pnp_cancel_remove_device,
	pnp_stop_device,
	pnp_query_stop_device,
	pnp_cancel_stop_device,

	pnp_query_device_relations,
	pnp_query_interface,
	pnp_query_capabilities,
	pnp_query_resources,
	pnp_query_resource_requirements,
	pnp_query_device_text,
	pnp_filter_resource_requirements,

	pnp_0x0E, // 0x0E, undefined

	pnp_read_config,
	pnp_write_config,
	pnp_eject,
	pnp_set_lock,
	pnp_query_id,
	pnp_query_pnp_device_state,
	pnp_query_bus_information,
	pnp_device_usage_notification,
	pnp_surprise_removal,

	pnp_query_legacy_bus_information, // IRP_MN_QUERY_LEGACY_BUS_INFORMATION
	pnp_device_enumerated // IRP_MN_DEVICE_ENUMERATED, since WIN7
};

} // namespace

extern "C" PAGEABLE NTSTATUS vhci_pnp(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();

	vdev_t *vdev = devobj_to_vdev(devobj);
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceCall("%!vdev_type_t!: enter irql %!irql!, %!pnpmn!",
			vdev->type, KeGetCurrentIrql(), irpstack->MinorFunction);

	NTSTATUS status = STATUS_SUCCESS;

	if (vdev->DevicePnPState == Deleted) { // the driver should not pass the IRP down to the next lower driver
		status = irp_done(irp, STATUS_NO_SUCH_DEVICE);
	} else if (irpstack->MinorFunction < ARRAYSIZE(pnpmn_functions)) {
		status = pnpmn_functions[irpstack->MinorFunction](vdev, irp);
	} else {
		Trace(TRACE_LEVEL_WARNING, "%!vdev_type_t!: unknown MinorFunction %!pnpmn!", vdev->type, irpstack->MinorFunction);
		status = irp_done_iostatus(irp);
	}

	TraceCall("%!vdev_type_t!: leave %!STATUS!", vdev->type, status);
	return status;
}
