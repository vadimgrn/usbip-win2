#include "plugin.h"
#include "trace.h"
#include "plugin.tmh"

#include "strutil.h"
#include "vhub.h"
#include "pnp.h"
#include "usb_util.h"
#include "usbdsc.h"
#include "vhci.h"
#include "pnp_remove.h"

namespace
{

PAGEABLE auto vhci_init_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	vpdo->current_intf_num = 0;
	vpdo->current_intf_alt = 0;

	INITIALIZE_PNP_STATE(vpdo);

	// vpdo usually starts its life at D3
	vpdo->DevicePowerState = PowerDeviceD3;
	vpdo->SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo->head_urbr);
	InitializeListHead(&vpdo->head_urbr_pending);
	InitializeListHead(&vpdo->head_urbr_sent);
	KeInitializeSpinLock(&vpdo->lock_urbr);

	auto &Flags = to_devobj(vpdo)->Flags;
	Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	if (!vhub_attach_vpdo(vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "Can't acquire free port");
		return STATUS_END_OF_FILE;
	}

	Flags &= ~DO_DEVICE_INITIALIZING; // should be the last step in initialization
	return STATUS_SUCCESS;
}

PAGEABLE auto setup_vpdo_with_descriptor(vpdo_dev_t *vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
	PAGED_CODE();

	if (is_valid_dsc(&d)) {
		NT_ASSERT(!is_valid_dsc(&vpdo->descriptor)); // first time initialization
		RtlCopyMemory(&vpdo->descriptor, &d, sizeof(d));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid device descriptor");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->speed = get_usb_speed(d.bcdUSB);

	vpdo->bDeviceClass = d.bDeviceClass;
	vpdo->bDeviceSubClass = d.bDeviceSubClass;
	vpdo->bDeviceProtocol = d.bDeviceProtocol;

	return STATUS_SUCCESS;
}

/*
* Many devices have 0 usb class number in a device descriptor.
* 0 value means that class number is determined at interface level.
* USB class, subclass and protocol numbers should be setup before importing.
* Because windows vhci driver builds a device compatible id with those numbers.
*/
PAGEABLE auto setup_vpdo_with_dsc_conf(vpdo_dev_t *vpdo, const USB_CONFIGURATION_DESCRIPTOR &d)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo->actconfig); // first time initialization
	vpdo->actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePoolWithTag(PagedPool, d.wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo->actconfig) {
		RtlCopyMemory(vpdo->actconfig, &d, d.wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate configuration descriptor, wTotalLength %d", d.wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (vpdo->bDeviceClass || vpdo->bDeviceSubClass || vpdo->bDeviceProtocol) {
		return STATUS_SUCCESS;
	}

	if (auto i = dsc_find_next_intf(vpdo->actconfig, nullptr)) {
		vpdo->bDeviceClass = i->bInterfaceClass;
		vpdo->bDeviceSubClass = i->bInterfaceSubClass;
		vpdo->bDeviceProtocol = i->bInterfaceProtocol;
		Trace(TRACE_LEVEL_VERBOSE, "Set Class(%#02x)/SubClass(%#02x)/Protocol(%#02x) from bInterfaceNumber %d, bAlternateSetting %d",
					vpdo->bDeviceClass, vpdo->bDeviceSubClass, vpdo->bDeviceProtocol,
					i->bInterfaceNumber, i->bAlternateSetting);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, vhci_pluginfo_t *pluginfo, ULONG inlen, FILE_OBJECT *fo)
{
	PAGED_CODE();

	if (inlen < sizeof(*pluginfo)) {
		Trace(TRACE_LEVEL_ERROR, "too small input length: %lld < %lld", inlen, sizeof(*pluginfo));
		return STATUS_INVALID_PARAMETER;
	}

	auto wTotalLength = pluginfo->dscr_conf.wTotalLength;

	if (inlen != sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf)) {
		Trace(TRACE_LEVEL_ERROR, "invalid pluginfo format: %lld != %lld", inlen, sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf));
		return STATUS_INVALID_PARAMETER;
	}

	auto devobj = vdev_create(to_devobj(vhci)->DriverObject, VDEV_VPDO);
	if (!devobj) {
		return STATUS_UNSUCCESSFUL;
	}

	auto vpdo = devobj_to_vpdo_or_null(devobj);

	vpdo->devid = pluginfo->devid;
	vpdo->parent = vhub_from_vhci(vhci);

	if (auto err = setup_vpdo_with_descriptor(vpdo, pluginfo->dscr_dev)) {
		destroy_device(vpdo);
		return err;
	}
	
	if (auto err = setup_vpdo_with_dsc_conf(vpdo, pluginfo->dscr_conf)) {
		destroy_device(vpdo);
		return err;
	}

	if (auto err = vhci_init_vpdo(vpdo)) {
		destroy_device(vpdo);
		return err;
	}

	NT_ASSERT(vpdo->port); // was assigned
	pluginfo->port = static_cast<char>(vpdo->port);
	NT_ASSERT(pluginfo->port == vpdo->port);

	vpdo->SerialNumberUser = *pluginfo->wserial ? libdrv_strdupW(pluginfo->wserial) : nullptr;

	if (InterlockedCompareExchangePointer(&fo->FsContext, vpdo, nullptr)) {
		Trace(TRACE_LEVEL_INFORMATION, "You can't plugin again");
		destroy_device(vpdo);
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->fo = fo;
	
	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port)
{
	PAGED_CODE();

	auto vhub = vhub_from_vhci(vhci);
	if (!vhub) {
		Trace(TRACE_LEVEL_INFORMATION, "vhub has gone");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port < 0) {
		Trace(TRACE_LEVEL_INFORMATION, "Plugging out all the devices");
		vhub_unplug_all_vpdo(vhub);
		return STATUS_SUCCESS;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, port)) {
		return vhub_unplug_vpdo(vpdo);
	}

	Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", port);
	return STATUS_NO_SUCH_DEVICE;
}
