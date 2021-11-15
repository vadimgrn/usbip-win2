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
#include "stub_driver.tmh"

#include "stub_dbg.h"
#include "stub_dev.h"

NTSTATUS stub_add_device(PDRIVER_OBJECT drvobj, PDEVICE_OBJECT pdo);
NTSTATUS stub_dispatch(PDEVICE_OBJECT devobj, IRP *irp);

static VOID
stub_unload(DRIVER_OBJECT *drvobj)
{
	TraceInfo(TRACE_GENERAL, "Enter\n");
	WPP_CLEANUP(drvobj);
}

NTSTATUS
DriverEntry(DRIVER_OBJECT *drvobj, UNICODE_STRING *regpath)
{
	WPP_INIT_TRACING(drvobj, regpath);
	TraceInfo(TRACE_GENERAL, "Enter\n");

	/* initialize the driver object's dispatch table */
	for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		drvobj->MajorFunction[i] = stub_dispatch;
	}

	drvobj->DriverExtension->AddDevice = stub_add_device;
	drvobj->DriverUnload = stub_unload;

	return STATUS_SUCCESS;
}