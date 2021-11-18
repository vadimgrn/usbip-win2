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
#include "stub_dispatch.tmh"

#include "stub_dbg.h"
#include "stub_irp.h"

NTSTATUS stub_dispatch_pnp(usbip_stub_dev_t *devstub, IRP *irp);
NTSTATUS stub_dispatch_power(usbip_stub_dev_t *devstub, IRP *irp);
NTSTATUS stub_dispatch_ioctl(usbip_stub_dev_t *devstub, IRP *irp);
NTSTATUS stub_dispatch_read(usbip_stub_dev_t *devstub, IRP *irp);
NTSTATUS stub_dispatch_write(usbip_stub_dev_t *devstub, IRP *irp);

NTSTATUS
stub_dispatch(PDEVICE_OBJECT devobj, IRP *irp)
{
	usbip_stub_dev_t *devstub = devobj->DeviceExtension;
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceInfo(TRACE_DISPATCH, "%!pnpmj!", irpstack->MajorFunction);

	switch (irpstack->MajorFunction) {
	case IRP_MJ_PNP:
		return stub_dispatch_pnp(devstub, irp);
	case IRP_MJ_POWER:
		// ID: 2960644 (farthen)
		// You can't set the power state if the device is not handled at all
		if (devstub->next_stack_dev == NULL) {
			return complete_irp(irp, STATUS_INVALID_DEVICE_STATE, 0);
		}
		return stub_dispatch_power(devstub, irp);
	case IRP_MJ_DEVICE_CONTROL:
		return stub_dispatch_ioctl(devstub, irp);
	case IRP_MJ_READ:
		return stub_dispatch_read(devstub, irp);
	case IRP_MJ_WRITE:
		return stub_dispatch_write(devstub, irp);
	default:
		return pass_irp_down(devstub, irp, NULL, NULL);
	}
}
