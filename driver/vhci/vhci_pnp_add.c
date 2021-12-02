#include "vhci_pnp.h"
#include "trace.h"
#include "vhci_pnp_add.tmh"

#include "vhci.h"

#define MAX_HUB_PORTS		6

static PAGEABLE BOOLEAN
is_valid_vdev_hwid(PDEVICE_OBJECT devobj)
{
	LPWSTR	hwid;
	UNICODE_STRING	ustr_hwid_devprop, ustr_hwid;
	BOOLEAN	res;

	hwid = get_device_prop(devobj, DevicePropertyHardwareID, NULL);
	if (hwid == NULL)
		return FALSE;

	RtlInitUnicodeString(&ustr_hwid_devprop, hwid);

	RtlInitUnicodeString(&ustr_hwid, HWID_ROOT);
	res = RtlEqualUnicodeString(&ustr_hwid, &ustr_hwid_devprop, TRUE);
	if (!res) {
		RtlInitUnicodeString(&ustr_hwid, HWID_VHCI);
		res = RtlEqualUnicodeString(&ustr_hwid, &ustr_hwid_devprop, TRUE);
		if (!res) {
			RtlInitUnicodeString(&ustr_hwid, HWID_VHUB);
			res = RtlEqualUnicodeString(&ustr_hwid, &ustr_hwid_devprop, TRUE);
		}
	}
	ExFreePoolWithTag(hwid, USBIP_VHCI_POOL_TAG);
	return res;
}

static PAGEABLE vdev_t *get_vdev_from_driver(DRIVER_OBJECT *drvobj, vdev_type_t type)
{
	for (DEVICE_OBJECT *devobj = drvobj->DeviceObject; devobj; devobj = devobj->NextDevice) {
		vdev_t *vdev = devobj_to_vdev(devobj);
		if (vdev->type == type) {
			return vdev;
		}
	}

	return NULL;
}

static PAGEABLE pvdev_t
create_child_pdo(pvdev_t vdev, vdev_type_t type)
{
	PAGED_CODE();

	TraceInfo(TRACE_VHUB, "creating child %!vdev_type_t!", type);

	DEVICE_OBJECT *devobj = vdev_create(vdev->Self->DriverObject, type);
	if (!devobj) {
		return NULL;
	}

	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	vdev_t *vdev_child = devobj_to_vdev(devobj);
	vdev_child->parent = vdev;

	return vdev_child;
}

static PAGEABLE void
init_dev_root(pvdev_t vdev)
{
	vdev->child_pdo = create_child_pdo(vdev, VDEV_CPDO);
}

static PAGEABLE void
init_dev_vhci(pvdev_t vdev)
{
	pvhci_dev_t	vhci = (pvhci_dev_t)vdev;

	vdev->child_pdo = create_child_pdo(vdev, VDEV_HPDO);
	RtlUnicodeStringInitEx(&vhci->DevIntfVhci, NULL, STRSAFE_IGNORE_NULLS);
	RtlUnicodeStringInitEx(&vhci->DevIntfUSBHC, NULL, STRSAFE_IGNORE_NULLS);
}

static PAGEABLE void
init_dev_vhub(pvdev_t vdev)
{
	pvhub_dev_t	vhub = (pvhub_dev_t)vdev;

	ExInitializeFastMutex(&vhub->Mutex);
	InitializeListHead(&vhub->head_vpdo);

	vhub->OutstandingIO = 1;

	// Initialize the remove event to Not-Signaled.  This event
	// will be set when the OutstandingIO will become 0.
	KeInitializeEvent(&vhub->RemoveEvent, SynchronizationEvent, FALSE);

	vhub->n_max_ports = MAX_HUB_PORTS;
}

static PAGEABLE NTSTATUS
add_vdev(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo, vdev_type_t type)
{
	PAGED_CODE();

	TraceInfo(TRACE_PNP, "adding %!vdev_type_t!: pdo: %p", type, pdo);

	DEVICE_OBJECT *devobj = vdev_create(drvobj, type);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	vdev_t *vdev = devobj_to_vdev(devobj);
	vdev->pdo = pdo;

	if (type != VDEV_ROOT) {
		vdev_t *vdev_pdo = devobj_to_vdev(vdev->pdo);
		vdev->parent = vdev_pdo->parent;
		vdev_pdo->fdo = vdev;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vdev->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (vdev->devobj_lower == NULL) {
		TraceError(TRACE_PNP, "failed to attach device stack");
		IoDeleteDevice(devobj);
		return STATUS_NO_SUCH_DEVICE;
	}

	switch (type) {
	case VDEV_ROOT:
		init_dev_root(vdev);
		break;
	case VDEV_VHCI:
		init_dev_vhci(vdev);
		break;
	case VDEV_VHUB:
		init_dev_vhub(vdev);
		break;
	}

	TraceInfo(TRACE_PNP, "%!vdev_type_t! added: vdev: %p", type, vdev);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo)
{
	PAGED_CODE();

	if (!is_valid_vdev_hwid(pdo)) {
		TraceError(TRACE_PNP, "invalid hw id");
		return STATUS_INVALID_PARAMETER;
	}

	root_dev_t *root = (root_dev_t*)get_vdev_from_driver(drvobj, VDEV_ROOT);
	vhci_dev_t *vhci = NULL;
	vdev_type_t type;

	if (root) {
		vhci = (vhci_dev_t*)get_vdev_from_driver(drvobj, VDEV_VHCI);
		type = vhci ? VDEV_VHUB : VDEV_VHCI;
	} else {
		type = VDEV_ROOT;
	}

	TraceInfo(TRACE_GENERAL, "irql %!irql!", KeGetCurrentIrql());
	return add_vdev(drvobj, pdo, type);
}
