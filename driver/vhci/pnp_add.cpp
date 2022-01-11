#include "pnp.h"
#include "trace.h"
#include "pnp_add.tmh"

#include "vhci.h"
#include <ntstrsafe.h>

namespace
{

PAGEABLE BOOLEAN is_valid_vdev_hwid(PDEVICE_OBJECT devobj)
{
	PAGED_CODE();

	LPWSTR	hwid;
	UNICODE_STRING	ustr_hwid_devprop, ustr_hwid;
	BOOLEAN	res;

	hwid = get_device_prop(devobj, DevicePropertyHardwareID, nullptr);
	if (hwid == nullptr)
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

PAGEABLE vdev_t *get_vdev_from_driver(DRIVER_OBJECT *drvobj, vdev_type_t type)
{
	PAGED_CODE();

	for (DEVICE_OBJECT *devobj = drvobj->DeviceObject; devobj; devobj = devobj->NextDevice) {
		vdev_t *vdev = devobj_to_vdev(devobj);
		if (vdev->type == type) {
			return vdev;
		}
	}

	return nullptr;
}

PAGEABLE vdev_t *create_child_pdo(vdev_t * vdev, vdev_type_t type)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "creating child %!vdev_type_t!", type);

	DEVICE_OBJECT *devobj = vdev_create(vdev->Self->DriverObject, type);
	if (!devobj) {
		return nullptr;
	}

	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	vdev_t *vdev_child = devobj_to_vdev(devobj);
	vdev_child->parent = vdev;

	return vdev_child;
}

PAGEABLE void init_dev_root(vdev_t * vdev)
{
	PAGED_CODE();
	vdev->child_pdo = create_child_pdo(vdev, VDEV_CPDO);
}

PAGEABLE void init_dev_vhci(vdev_t *vdev)
{
	PAGED_CODE();

	auto vhci = (vhci_dev_t*)vdev;
	vdev->child_pdo = create_child_pdo(vdev, VDEV_HPDO);

	RtlUnicodeStringInitEx(&vhci->DevIntfVhci, nullptr, STRSAFE_IGNORE_NULLS);
	RtlUnicodeStringInitEx(&vhci->DevIntfUSBHC, nullptr, STRSAFE_IGNORE_NULLS);
}

PAGEABLE void init_dev_vhub(vdev_t *vdev)
{
	PAGED_CODE();

	auto vhub = static_cast<vhub_dev_t*>(vdev);
	ExInitializeFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS add_vdev(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo, vdev_type_t type)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "adding %!vdev_type_t!: pdo: %p", type, pdo);

	auto devobj = vdev_create(drvobj, type);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vdev = devobj_to_vdev(devobj);
	vdev->pdo = pdo;

	if (type != VDEV_ROOT) {
		auto vdev_pdo = devobj_to_vdev(vdev->pdo);
		vdev->parent = vdev_pdo->parent;
		vdev_pdo->fdo = vdev;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vdev->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (!vdev->devobj_lower) {
		Trace(TRACE_LEVEL_ERROR, "IoAttachDeviceToDeviceStack error");
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

	Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t! added %p", type, vdev);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}

} // namespace

extern "C" PAGEABLE NTSTATUS vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo)
{
	PAGED_CODE();

	if (!is_valid_vdev_hwid(pdo)) {
		Trace(TRACE_LEVEL_ERROR, "Invalid hwid");
		return STATUS_INVALID_PARAMETER;
	}

	auto type = VDEV_ROOT;

	if (auto root = (root_dev_t*)get_vdev_from_driver(drvobj, VDEV_ROOT)) {
		auto vhci = (vhci_dev_t*)get_vdev_from_driver(drvobj, VDEV_VHCI);
		type = vhci ? VDEV_VHUB : VDEV_VHCI;
	}

	Trace(TRACE_LEVEL_VERBOSE, "irql %!irql!", KeGetCurrentIrql());

	return add_vdev(drvobj, pdo, type);
}
