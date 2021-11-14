/* libusb-win32, Generic Windows USB Library
* Copyright (c) 2010 Travis Robinson <libusbdotnet@gmail.com>
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
#include "stub_read.tmh"

#include "stub_dbg.h"
#include "usbip_proto.h"

#include "stub_res.h"

static struct usbip_header *
get_usbip_hdr_from_read_irp(PIRP irp)
{
	PIO_STACK_LOCATION	irpstack;
	ULONG	len;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	len = irpstack->Parameters.Read.Length;
	if (len < sizeof(struct usbip_header)) {
		return NULL;
	}
	irp->IoStatus.Information = len;
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

NTSTATUS
stub_dispatch_read(usbip_stub_dev_t *devstub, IRP *irp)
{
	TraceInfo(TRACE_READWRITE, "enter\n");
	return collect_done_stub_res(devstub, irp);
}