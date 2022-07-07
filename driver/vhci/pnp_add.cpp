#include "pnp.h"
#include "trace.h"
#include "pnp_add.tmh"

#include "vhci.h"
#include "csq.h"
#include "pnp_remove.h"

#include <ntstrsafe.h>

namespace
{

PAGEABLE auto is_valid_vdev_hwid(DEVICE_OBJECT *devobj)
{
	PAGED_CODE();

	NTSTATUS err{};
	ULONG dummy;
	auto hwid_wstr = (PWSTR)GetDeviceProperty(devobj, DevicePropertyHardwareID, err, dummy);
	if (!hwid_wstr) {
		return err;
	}

	UNICODE_STRING hwid{};
	RtlInitUnicodeString(&hwid, hwid_wstr);
	TraceDbg("%!USTR!", &hwid);

	const wchar_t* v[] = { HWID_ROOT, HWID_VHCI, HWID_VHUB };
	
	for (err = STATUS_INVALID_PARAMETER; auto i: v) {
		UNICODE_STRING s{};
		RtlInitUnicodeString(&s, i);
		if (RtlEqualUnicodeString(&s, &hwid, true)) {
			err = STATUS_SUCCESS;
			break;
		}
	}

	ExFreePoolWithTag(hwid_wstr, USBIP_VHCI_POOL_TAG);
	return err;
}

PAGEABLE vdev_t *get_vdev_from_driver(DRIVER_OBJECT *drvobj, vdev_type_t type)
{
	PAGED_CODE();

	for (auto devobj = drvobj->DeviceObject; devobj; devobj = devobj->NextDevice) {
		auto vdev = to_vdev(devobj);
		if (vdev->type == type) {
			return vdev;
		}
	}

	return nullptr;
}

PAGEABLE vdev_t *create_child_pdo(vdev_t *vdev, vdev_type_t type)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "creating child %!vdev_type_t!", type);

	auto devobj = vdev_create(vdev->Self->DriverObject, type);
	if (!devobj) {
		return nullptr;
	}

	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	auto vdev_child = to_vdev(devobj);
	vdev_child->parent = vdev;

	return vdev_child;
}

PAGEABLE auto init(vhci_dev_t &vhci)
{
	PAGED_CODE();

	vhci.child_pdo = create_child_pdo(&vhci, VDEV_HPDO);
        if (!vhci.child_pdo) {
                return STATUS_UNSUCCESSFUL;
        }

	RtlUnicodeStringInitEx(&vhci.DevIntfVhci, nullptr, STRSAFE_IGNORE_NULLS);
	RtlUnicodeStringInitEx(&vhci.DevIntfUSBHC, nullptr, STRSAFE_IGNORE_NULLS);

        return STATUS_SUCCESS;
}

PAGEABLE auto init(vhub_dev_t &vhub)
{
        PAGED_CODE();

        ExInitializeFastMutex(&vhub.Mutex);
        RtlUnicodeStringInitEx(&vhub.DevIntfRootHub, nullptr, STRSAFE_IGNORE_NULLS);

        return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(yes))
PAGEABLE auto add_vdev(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo, vdev_type_t type)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "pdo %04x, %!vdev_type_t!", ptr4log(pdo), type);

	auto devobj = vdev_create(drvobj, type);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vdev = to_vdev(devobj);
	vdev->pdo = pdo;

	if (type != VDEV_ROOT) {
		auto vdev_pdo = to_vdev(vdev->pdo);
		vdev->parent = vdev_pdo->parent;
		vdev_pdo->fdo = vdev;
	}

	// Attach our vhub to the device stack. The return value of IoAttachDeviceToDeviceStack 
        // is the top of the attachment chain. This is where all the IRPs should be routed.

	vdev->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (!vdev->devobj_lower) {
		Trace(TRACE_LEVEL_ERROR, "IoAttachDeviceToDeviceStack error");
		IoDeleteDevice(devobj);
		return STATUS_NO_SUCH_DEVICE;
	}

        auto err = STATUS_SUCCESS;

	switch (type) {
	case VDEV_ROOT:
                vdev->child_pdo = create_child_pdo(vdev, VDEV_CPDO);
		break;
	case VDEV_VHCI:
		err = init(*reinterpret_cast<vhci_dev_t*>(vdev));
		break;
	case VDEV_VHUB:
                err = init(*reinterpret_cast<vhub_dev_t*>(vdev));
                break;
	}

        Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!(%04x) -> %!STATUS!", type, ptr4log(vdev), err);

        if (err) {
                destroy_device(vdev);
        } else {
                devobj->Flags &= ~DO_DEVICE_INITIALIZING; // should be the final step in the AddDevice process
        }

        return err;
}

} // namespace


_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
extern "C" PAGEABLE NTSTATUS vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo)
{
	PAGED_CODE();

	if (auto err = is_valid_vdev_hwid(pdo)) {
		Trace(TRACE_LEVEL_ERROR, "Invalid hwid, %!STATUS!", err);
		return err;
	}

	auto type = VDEV_ROOT;

	if (auto root = (root_dev_t*)get_vdev_from_driver(drvobj, VDEV_ROOT)) {
		auto vhci = (vhci_dev_t*)get_vdev_from_driver(drvobj, VDEV_VHCI);
		type = vhci ? VDEV_VHUB : VDEV_VHCI;
	}

	TraceMsg("irql %!irql!", KeGetCurrentIrql());

	return add_vdev(drvobj, pdo, type);
}
