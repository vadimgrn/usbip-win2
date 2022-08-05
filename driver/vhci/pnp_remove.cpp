#include "pnp_remove.h"
#include "pnp_remove.h"
#include <wdm.h>
#include "trace.h"
#include "pnp_remove.tmh"

#include "vhci.h"
#include "pnp.h"
#include "irp.h"
#include "wmi.h"
#include "vhub.h"
#include "usbip_vhci_api.h"
#include "strutil.h"
#include "csq.h"
#include "wsk_cpp.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_vhci(vhci_dev_t &vhci)
{
	PAGED_CODE();

	TraceMsg("%04x", ptr4log(&vhci));

        IoSetDeviceInterfaceState(&vhci.DevIntfVhci, false);
	IoSetDeviceInterfaceState(&vhci.DevIntfUSBHC, false);
	RtlFreeUnicodeString(&vhci.DevIntfVhci);

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(&vhci);

	Trace(TRACE_LEVEL_INFORMATION, "Invalidating vhci device object %04x", ptr4log(vhci.Self));
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_vhub(vhub_dev_t &vhub)
{
	PAGED_CODE();

	TraceMsg("%04x", ptr4log(&vhub));

	IoSetDeviceInterfaceState(&vhub.DevIntfRootHub, false);
	RtlFreeUnicodeString(&vhub.DevIntfRootHub);

	// At this point, vhub should has no vpdo. With this assumption, there's no need to remove all vpdos.
	for (int i = 0; i < vhub.NUM_PORTS; ++i) {
		if (vhub.vpdo[i]) {
			Trace(TRACE_LEVEL_ERROR, "Port #%d is acquired", i);
		}
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void free_usb_dev_interface(UNICODE_STRING &symlink_name)
{
        PAGED_CODE();

        if (symlink_name.Buffer) {
                if (auto err = IoSetDeviceInterfaceState(&symlink_name, false)) {
                        Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState %!STATUS!", err);
                }
        }

        RtlFreeUnicodeString(&symlink_name);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void free_string_descriptors(_Inout_ vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	for (auto &d: vpdo.strings) {
		if (d) {
			ExFreePoolWithTag(d, USBIP_VHCI_POOL_TAG);
			d = nullptr;
		}
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void free_strings(vpdo_dev_t &d)
{
	PAGED_CODE();

        libdrv_free(d.busid);
        d.busid = nullptr;

        RtlFreeUnicodeString(&d.node_name);
        RtlFreeUnicodeString(&d.service_name);
        RtlFreeUnicodeString(&d.serial);

        free_usb_dev_interface(d.usb_dev_interface);
}

/*
 * The socket is closed, there is no concurrency with send_complete from internal_ioctl.cpp
 */
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void cancel_pending_irps(vpdo_dev_t &vpdo)
{
	PAGED_CODE();
	NT_ASSERT(!vpdo.sock);

	TraceMsg("%04x", ptr4log(&vpdo));

	if (is_initialized(vpdo.irps_csq)) {
                while (auto irp = IoCsqRemoveNextIrp(&vpdo.irps_csq, nullptr)) {
			complete_as_canceled(irp);
                }
        }
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void close_socket(vpdo_dev_t &vpdo)
{
        PAGED_CODE();

        if (!vpdo.sock) {
		return;
	}
		
	if (auto err = event_callback_control(vpdo.sock, WSK_EVENT_DISABLE | WSK_EVENT_DISCONNECT, true)) {
		Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", err);
	}

	if (auto err = disconnect(vpdo.sock)) {
                Trace(TRACE_LEVEL_ERROR, "disconnect %!STATUS!", err);
        }

	if (auto err = close(vpdo.sock)) {
                Trace(TRACE_LEVEL_ERROR, "close %!STATUS!", err);
        }

	vpdo.sock = nullptr;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_vpdo(vpdo_dev_t &vpdo)
{
	PAGED_CODE();
	TraceMsg("%04x, port %d", ptr4log(&vpdo), vpdo.port);

	close_socket(vpdo);
	cancel_pending_irps(vpdo);

	vhub_detach_vpdo(&vpdo);

	free_strings(vpdo);
	free_string_descriptors(vpdo);

	if (auto &wi = vpdo.workitem) {
		IoFreeWorkItem(wi);
		wi = nullptr;
	}

	if (vpdo.actconfig) {
		ExFreePoolWithTag(vpdo.actconfig, USBIP_VHCI_POOL_TAG);
                vpdo.actconfig = nullptr;
	}
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_device(vdev_t *vdev)
{
	PAGED_CODE();

        if (!vdev) {
                return;
        }

	TraceMsg("%!vdev_type_t! %04x", vdev->type, ptr4log(vdev));

	if (vdev->child_pdo) {
		vdev->child_pdo->parent = nullptr;
		if (vdev->child_pdo->fdo) {
			vdev->child_pdo->fdo->parent = nullptr;
		}
	}

	if (vdev->fdo) {
		vdev->fdo->pdo = nullptr;
	}

	if (vdev->pdo && vdev->type != VDEV_ROOT) {
		to_vdev(vdev->pdo)->fdo = nullptr;
	}

	if (auto n = vdev->intf_ref_cnt) {
		Trace(TRACE_LEVEL_WARNING, "Interface reference count %ld", n);
	}

	switch (vdev->type) {
	case VDEV_VHCI:
		destroy_vhci(*(vhci_dev_t*)vdev);
		break;
	case VDEV_VHUB:
		destroy_vhub(*(vhub_dev_t*)vdev);
		break;
	case VDEV_VPDO:
		destroy_vpdo(*(vpdo_dev_t*)vdev);
		break;
	}

	if (vdev->devobj_lower) { // detach from the underlying devices
		IoDetachDevice(vdev->devobj_lower);
		vdev->devobj_lower = nullptr;
	}

	IoDeleteDevice(vdev->Self);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_remove_device(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	TraceMsg("%!vdev_type_t!(%04x)", vdev->type, ptr4log(vdev));

	if (vdev->type == VDEV_VPDO) {
		vhub_unplug_vpdo(static_cast<vpdo_dev_t*>(vdev));
	}

	if (vdev->PnPState != pnp_state::Removed) {
		set_state(*vdev, pnp_state::Removed);
	} else {
		TraceMsg("%!vdev_type_t!(%04x) already removed", vdev->type, ptr4log(vdev));
		return CompleteRequest(irp);
	}

	POWER_STATE ps{ .DeviceState = PowerDeviceD3 };
	vdev->DevicePowerState = ps.DeviceState;
	PoSetPowerState(vdev->Self, DevicePowerState, ps);

	auto st = irp_pass_down_or_complete(vdev, irp);
	destroy_device(vdev);
	return st;
}
