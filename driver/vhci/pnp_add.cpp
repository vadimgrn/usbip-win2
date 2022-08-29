#include "pnp_add.h"
#include "trace.h"
#include "pnp_add.tmh"

#include "dev.h"
#include "vhci.h"
#include "csq.h"
#include "pnp_id.h"
#include "pnp_remove.h"

#include <ntstrsafe.h>

namespace
{

PAGEABLE auto get_usb_version(_Out_ vdev_usb_t &version, _In_ DEVICE_OBJECT *devobj)
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

	const wchar_t* v[] { 
		HWID_ROOT1, HWID_EHCI, HWID_VHUB,  // VDEV_USB2
		HWID_ROOT2, HWID_XHCI, HWID_VHUB30 // VDEV_USB3
	};

	err = STATUS_INVALID_PARAMETER;

	for (int i = 0; i < ARRAYSIZE(v); ++i) {

		UNICODE_STRING s{};
		RtlInitUnicodeString(&s, v[i]);

		if (RtlEqualUnicodeString(&s, &hwid, true)) {
			version = i > 2 ? VDEV_USB3 : VDEV_USB2;
			err = STATUS_SUCCESS;
			break;
		}
	}

	TraceDbg("%04x %!USTR! -> %!STATUS!, %!vdev_usb_t!", ptr4log(devobj), &hwid, err, version);

	ExFreePoolWithTag(hwid_wstr, USBIP_VHCI_POOL_TAG);
	return err;
}

PAGEABLE vdev_t *get_vdev_from_driver(_In_ DRIVER_OBJECT *drvobj, _In_ vdev_usb_t version, _In_ vdev_type_t type)
{
	PAGED_CODE();

	for (auto devobj = drvobj->DeviceObject; devobj; devobj = devobj->NextDevice) {
		auto vdev = to_vdev(devobj);
		if (vdev->version == version && vdev->type == type) {
			return vdev;
		}
	}

	return nullptr;
}

PAGEABLE vdev_t *create_child_pdo(_In_ vdev_t *vdev, _In_ vdev_type_t type)
{
	PAGED_CODE();

	TraceDbg("%!vdev_type_t!", type);

	auto devobj = vdev_create(vdev->Self->DriverObject, vdev->version, type);
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

PAGEABLE auto init(_Inout_ vhub_dev_t &vhub)
{
        PAGED_CODE();

        ExInitializeFastMutex(&vhub.mutex);
        RtlUnicodeStringInitEx(&vhub.DevIntfRootHub, nullptr, STRSAFE_IGNORE_NULLS);

        return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(yes))
PAGEABLE auto add_vdev(
	_In_ DRIVER_OBJECT *DriverObject, _In_ DEVICE_OBJECT *PhysicalDeviceObject, 
	_In_ vdev_usb_t version, _In_ vdev_type_t type)
{
	PAGED_CODE();

	auto devobj = vdev_create(DriverObject, version, type);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vdev = to_vdev(devobj);
	vdev->pdo = PhysicalDeviceObject;

	if (type != VDEV_ROOT) {
		auto vdev_pdo = to_vdev(vdev->pdo);
		vdev->parent = vdev_pdo->parent;
		vdev_pdo->fdo = vdev;
	}

	vdev->devobj_lower = IoAttachDeviceToDeviceStack(devobj, PhysicalDeviceObject);
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

        TraceMsg("%!vdev_usb_t!, %!vdev_type_t!(%04x) -> %!STATUS!", version, type, ptr4log(vdev), err);

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
extern "C" PAGEABLE NTSTATUS vhci_add_device(_In_ DRIVER_OBJECT *DriverObject, _In_ DEVICE_OBJECT *PhysicalDeviceObject)
{
	PAGED_CODE();
	TraceDbg("PhysicalDeviceObject %04x", ptr4log(PhysicalDeviceObject));
	
	vdev_usb_t version{};

	if (auto err = get_usb_version(version, PhysicalDeviceObject)) {
		Trace(TRACE_LEVEL_ERROR, "Device not found by HWID, %!STATUS!", err);
		return err;
	}

	auto type = VDEV_ROOT;

	if (get_vdev_from_driver(DriverObject, version, type)) {
		type = !get_vdev_from_driver(DriverObject, version, VDEV_VHCI) ? VDEV_VHCI : VDEV_VHUB;
	}

	return add_vdev(DriverObject, PhysicalDeviceObject, version, type);
}
