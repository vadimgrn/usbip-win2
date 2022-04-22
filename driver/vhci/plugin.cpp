#include "plugin.h"
#include "trace.h"
#include "plugin.tmh"

#include "vhub.h"
#include "pnp.h"
#include "usb_util.h"
#include "usbdsc.h"
#include "vhci.h"
#include "pnp_remove.h"
#include "irp.h"
#include "csq.h"
#include "wsk_utils.h"

namespace
{

PAGEABLE auto init(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo.current_intf_num);
        NT_ASSERT(!vpdo.current_intf_alt);

        NT_ASSERT(vpdo.PnPState == pnp_state::NotStarted);
        NT_ASSERT(vpdo.PreviousPnPState == pnp_state::NotStarted);

	// vpdo usually starts its life at D3
	vpdo.DevicePowerState = PowerDeviceD3;
	vpdo.SystemPowerState = PowerSystemWorking;

	if (auto err = init_queues(vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "init_queues -> %!STATUS!", err);
		return err;
	}

	auto &Flags = vpdo.Self->Flags;
	Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	if (!vhub_attach_vpdo(&vpdo)) {
		Trace(TRACE_LEVEL_ERROR, "Can't acquire free usb port");
		return STATUS_END_OF_FILE;
	}

	Flags &= ~DO_DEVICE_INITIALIZING; // should be the last step in initialization
	return STATUS_SUCCESS;
}

PAGEABLE auto init(vpdo_dev_t &vpdo, const USB_DEVICE_DESCRIPTOR &d)
{
	PAGED_CODE();

	if (is_valid_dsc(&d)) {
		NT_ASSERT(!is_valid_dsc(&vpdo.descriptor)); // first time initialization
		RtlCopyMemory(&vpdo.descriptor, &d, sizeof(d));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid device descriptor");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.speed = get_usb_speed(d.bcdUSB);

	vpdo.bDeviceClass = d.bDeviceClass;
	vpdo.bDeviceSubClass = d.bDeviceSubClass;
	vpdo.bDeviceProtocol = d.bDeviceProtocol;

	return STATUS_SUCCESS;
}

PAGEABLE auto set_class_subclass_proto(vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	auto d = dsc_find_next_intf(vpdo.actconfig, nullptr);
	if (!d) {
		Trace(TRACE_LEVEL_ERROR, "Interface descriptor not found");
		return STATUS_INVALID_PARAMETER;
	}

	vpdo.bDeviceClass = d->bInterfaceClass;
	vpdo.bDeviceSubClass = d->bInterfaceSubClass;
	vpdo.bDeviceProtocol = d->bInterfaceProtocol;

	Trace(TRACE_LEVEL_INFORMATION, "Set Class(%#02x)/SubClass(%#02x)/Protocol(%#02x) from bInterfaceNumber %d, bAlternateSetting %d",
					vpdo.bDeviceClass, vpdo.bDeviceSubClass, vpdo.bDeviceProtocol,
					d->bInterfaceNumber, d->bAlternateSetting);

	return STATUS_SUCCESS;
}

/*
* Many devices have zero usb class number in a device descriptor.
* zero value means that class number is determined at interface level.
* USB class, subclass and protocol numbers should be setup before importing.
* Because windows vhci driver builds a device compatible id with those numbers.
*/
PAGEABLE auto init(vpdo_dev_t &vpdo, const USB_CONFIGURATION_DESCRIPTOR &d)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo.actconfig); // first time initialization
	vpdo.actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, 
                                                                        d.wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo.actconfig) {
		RtlCopyMemory(vpdo.actconfig, &d, d.wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %d bytes of memory", d.wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	return d.bNumInterfaces == 1 && !(vpdo.bDeviceClass || vpdo.bDeviceSubClass || vpdo.bDeviceProtocol) ?
		set_class_subclass_proto(vpdo) : STATUS_SUCCESS;
}

PAGEABLE auto copy(UNICODE_STRING &dst, const char *src)
{
        PAGED_CODE();

        UTF8_STRING s;
        RtlInitUTF8String(&s, src);

        return RtlUTF8StringToUnicodeString(&dst, &s, true);
}

PAGEABLE NTSTATUS create_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &r, ULONG &outlen)
{
        PAGED_CODE();

        outlen = 0;

        auto devobj = vdev_create(vhci->Self->DriverObject, VDEV_VPDO);
        if (!devobj) {
                return STATUS_UNSUCCESSFUL;
        }

        auto vpdo = to_vpdo_or_null(devobj);
        vpdo->parent = vhub_from_vhci(vhci);

        if (!*r.serial) {
                RtlInitUnicodeString(&vpdo->SerialNumberUser, nullptr);
        } else if (auto err = copy(vpdo->SerialNumberUser, r.serial)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.serial, err);
                destroy_device(vpdo);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

/*
        vpdo->devid = r.devid;

        if (auto err = init(*vpdo, r.dscr_dev)) {
                destroy_device(vpdo);
                return err;
        }

        if (auto err = init(*vpdo, r.dscr_conf)) {
                destroy_device(vpdo);
                return err;
        }
*/
        if (auto err = init(*vpdo)) {
                destroy_device(vpdo);
                return err;
        }

        NT_ASSERT(vpdo->port > 0); // was assigned
        r.port = vpdo->port;
        outlen = sizeof(r.port);

        IoInvalidateDeviceRelations(vhci->pdo, BusRelations); // kick PnP system
        return STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS vhci_plugin_vpdo(IRP *irp, vhci_dev_t*, ioctl_usbip_vhci_plugin &r)
{
	PAGED_CODE();
        TraceCall("irp %p: %s:%s, busid %s, serial '%s'", irp, r.host, r.tcp_port, r.busid, *r.serial ? r.serial : "");

        auto f = [] (auto &tr)
        {
                TraceCall("Version %d, SocketType %d, Protocol %u, AddressFamily %d, ProviderId %!GUID!", 
                        tr.Version, tr.SocketType, tr.Protocol, tr.AddressFamily, &tr.ProviderId);
                return false;
        };

        return STATUS_NOT_IMPLEMENTED; // IoCsqInsertIrpEx(&vhci->irps_csq, irp, nullptr, &r);
}

PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port)
{
	PAGED_CODE();

	auto vhub = vhub_from_vhci(vhci);
	if (!vhub) {
		Trace(TRACE_LEVEL_INFORMATION, "vhub has gone");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port <= 0) {
		Trace(TRACE_LEVEL_VERBOSE, "Plugging out all devices");
		vhub_unplug_all_vpdo(vhub);
		return STATUS_SUCCESS;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, port)) {
		return vhub_unplug_vpdo(vpdo);
	}

	Trace(TRACE_LEVEL_ERROR, "Invalid or empty port %d", port);
	return STATUS_NO_SUCH_DEVICE;
}
