/* libusb-win32, Generic Windows USB Library
 * Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "stub_driver.h"
#include "stub_trace.h"
#include "stub_pnp.tmh"

#include "stub_dbg.h"
#include "stub_irp.h"

static NTSTATUS
on_start_complete(DEVICE_OBJECT *devobj, IRP *irp, void *context)
{
	usbip_stub_dev_t	*devstub = (usbip_stub_dev_t *)devobj->DeviceExtension;

	UNREFERENCED_PARAMETER(context);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	devstub->is_started = TRUE;
	unlock_dev_removal(devstub);

	return STATUS_SUCCESS;
}

static void
disable_interface(usbip_stub_dev_t *devstub)
{
	NTSTATUS	status;

	if (devstub->interface_name.Buffer == NULL)
		return;

	status = IoSetDeviceInterfaceState(&devstub->interface_name, FALSE);
	if (NT_ERROR(status)) {
		TraceError(TRACE_PNP, "failed to disable interface: %!STATUS!\n", status);
	}
	if (devstub->interface_name.Buffer) {
		RtlFreeUnicodeString(&devstub->interface_name);
		devstub->interface_name.Buffer = NULL;
	}
}

NTSTATUS
stub_dispatch_pnp(usbip_stub_dev_t *devstub, IRP *irp)
{
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	TraceInfo(TRACE_DISPATCH, "%!pnpmn!\n", irpstack->MinorFunction);

	NTSTATUS status = lock_dev_removal(devstub);
	if (NT_ERROR(status)) {
		TraceInfo(TRACE_PNP, "device is pending removal: %s\n", dbg_devstub(devstub));
		return complete_irp(irp, status, 0);
	}

	switch (irpstack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		status = IoSetDeviceInterfaceState(&devstub->interface_name, TRUE);
		if (NT_ERROR(status)) {
			TraceError(TRACE_PNP, "IoSetDeviceInterfaceState %!STATUS!\n", status);
		}
		return pass_irp_down(devstub, irp, on_start_complete, NULL);
	case IRP_MN_REMOVE_DEVICE:
		disable_interface(devstub);

		devstub->is_started = FALSE;

		/* wait until all outstanding requests are finished */
		unlock_wait_dev_removal(devstub);

		/* USBD_CloseHandle should be ahead of pass_irp_down */
		USBD_CloseHandle(devstub->hUSBD);

		status = pass_irp_down(devstub, irp, NULL, NULL);

		TraceInfo(TRACE_PNP, "deleting device: %s\n", dbg_devstub(devstub));

		remove_devlink(devstub);
		free_devconf(devstub->devconf);
		devstub->devconf = NULL;

		/* delete the device object */
		IoDetachDevice(devstub->next_stack_dev);
		IoDeleteDevice(devstub->self);
		return status;
	case IRP_MN_SURPRISE_REMOVAL:
		devstub->is_started = FALSE;

		disable_interface(devstub);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_STOP_DEVICE:
		devstub->is_started = FALSE;
		break;
	default:
		break;
	}

	unlock_dev_removal(devstub);
	return pass_irp_down(devstub, irp, NULL, NULL);
}
