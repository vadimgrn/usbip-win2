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

static PAGEABLE pvdev_t
get_vdev_from_driver(PDRIVER_OBJECT drvobj, vdev_type_t type)
{
	PDEVICE_OBJECT	devobj = drvobj->DeviceObject;

	while (devobj) {
		if (DEVOBJ_VDEV_TYPE(devobj) == type)
			return DEVOBJ_TO_VDEV(devobj);
		devobj = devobj->NextDevice;
	}

	return NULL;
}

static PAGEABLE pvdev_t
create_child_pdo(pvdev_t vdev, vdev_type_t type)
{
	pvdev_t	vdev_child;
	PDEVICE_OBJECT	devobj;

	PAGED_CODE();

	TraceInfo(TRACE_VHUB, "creating child %!vdev_type_t!\n", type);

	if ((devobj = vdev_create(vdev->Self->DriverObject, type)) == NULL)
		return NULL;

	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	vdev_child = DEVOBJ_TO_VDEV(devobj);
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
	PDEVICE_OBJECT	devobj;
	pvdev_t		vdev;

	PAGED_CODE();

	TraceInfo(TRACE_PNP, "adding %!vdev_type_t!: pdo: 0x%p\n", type, pdo);

	devobj = vdev_create(drvobj, type);
	if (devobj == NULL)
		return STATUS_UNSUCCESSFUL;

	vdev = DEVOBJ_TO_VDEV(devobj);
	vdev->pdo = pdo;

	if (type != VDEV_ROOT) {
		pvdev_t	vdev_pdo = DEVOBJ_TO_VDEV(vdev->pdo);

		vdev->parent = vdev_pdo->parent;
		vdev_pdo->fdo = vdev;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vdev->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (vdev->devobj_lower == NULL) {
		TraceError(TRACE_PNP, "failed to attach device stack\n");
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
	default:
		break;
	}

	TraceInfo(TRACE_PNP, "%!vdev_type_t! added: vdev: %p\n", type, vdev);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo)
{
	proot_dev_t	root;
	pvhci_dev_t	vhci = NULL;
	vdev_type_t	type;

	PAGED_CODE();

	if (!is_valid_vdev_hwid(pdo)) {
		TraceError(TRACE_PNP, "invalid hw id\n");
		return STATUS_INVALID_PARAMETER;
	}

	root = (proot_dev_t)get_vdev_from_driver(drvobj, VDEV_ROOT);
	if (root == NULL)
		type = VDEV_ROOT;
	else {
		vhci = (pvhci_dev_t)get_vdev_from_driver(drvobj, VDEV_VHCI);
		type = (vhci == NULL) ? VDEV_VHCI: VDEV_VHUB;
	}
	return add_vdev(drvobj, pdo, type);
}
