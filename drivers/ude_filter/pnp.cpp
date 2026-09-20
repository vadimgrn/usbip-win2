/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "driver.h"
#include "irp.h"
#include "query_interface.h"

#include <libdrv/remove_lock.h>
#include <libdrv/unique_ptr.h>
#include <libdrv/ioctl.h>

#include <usbbusif.h>
#include <wdmguid.h>

#include <minwindef.h>
#include <ks.h>

#include <strmini.h>
#include <initguid.h>
#include <usbcamdi.h>

using namespace usbip;
using namespace libdrv;

namespace
{

using QueryInterface = decltype(_IO_STACK_LOCATION::Parameters.QueryInterface);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto contains(_In_ const DEVICE_RELATIONS &r, _In_ const DEVICE_OBJECT *obj)
{
	PAGED_CODE();
	NT_ASSERT(obj);

	for (ULONG i = 0; i < r.Count; ++i) {
		if (r.Objects[i] == obj) {
			return true;
		}
	}

	return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void query_bus_relations(_Inout_ filter_ext &fltr, _In_ const DEVICE_RELATIONS &r)
{
	PAGED_CODE();

	NT_ASSERT(fltr.is_hub);
	auto &previous = fltr.hub.previous;

	auto next = r.Count ? clone_relations_retry(r) : nullptr;
	if (r.Count && !next) {
		Trace(TRACE_LEVEL_ERROR, 
		      "hub %04x: failed to allocate relations buffer for %lu PDOs; skipping FiDO attachment", 
		      ptr04x(fltr.self), r.Count);
		return;
	}

	ULONG count{};

	for (ULONG i = 0; i < r.Count; ++i) {

                if (auto pdo = r.Objects[i]; !(previous && contains(*previous, pdo))) {

                        TraceDbg("Creating a FiDO for PDO %04x", ptr04x(pdo));

                        auto st = do_add_device(fltr.self->DriverObject, pdo, &fltr);
                        if (NT_ERROR(st)) {
				Trace(TRACE_LEVEL_ERROR, 
				      "Failed to add a FiDO for PDO %04x (%lu of %lu), %!STATUS! - device will lack URB fixup", 
				      ptr04x(pdo), i + 1, r.Count, st);
				ObDereferenceObject(next->Objects[i]);
				continue;
			}
		}

		next->Objects[count++] = next->Objects[i];
	}

        destroy_relations(previous);

	previous = next;
	if (previous) {
		previous->Count = count;
	}
}

/*
 * After we forward the request, the bus driver have created or deleted
 * a child device object. When bus driver created one (or more), this is the PDO
 * of our target device, we create and attach a filter object to it.
 * Note that we only attach the last detected USB device on its hub.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto query_bus_relations(_Inout_ filter_ext &fltr, _In_ IRP *irp)
{
	PAGED_CODE();

	auto st = ForwardIrpSynchronously(fltr, irp);
	if (NT_SUCCESS(st)) {
		if (auto r = reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information)) {
			query_bus_relations(fltr, *r);
		}
	}

	CompleteRequest(irp);
	return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto remove_device(_Inout_ filter_ext &fltr, _In_ IRP *irp, _In_ remove_lock_guard &lock)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(fltr.self));

	lock.release_and_wait(); // all allocated URBs are freed after this, USBD_HANDLE can be closed

	if (!fltr.is_hub) {
		if (auto &h = fltr.device.usbd_handle) {
			USBD_CloseHandle(h); // must be called before sending the IRP down the USB driver stack
			h = nullptr;
		}
	}

	auto st = ForwardIrp(fltr, irp); // drivers must not fail this IRP
	destroy(fltr);
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED const char* get_guid_name(_In_ const GUID &guid)
{
	PAGED_CODE();

	static const struct {
		const GUID &guid;
		const char *name;
	} v[] = {
		{GUID_D3COLD_SUPPORT_INTERFACE, "D3COLD_SUPPORT"},
		{GUID_PNP_EXTENDED_ADDRESS_INTERFACE, "PNP_EXTENDED_ADDRESS"},
		{GUID_PNP_LOCATION_INTERFACE, "PNP_LOCATION"},
		{GUID_IOMMU_BUS_INTERFACE, "IOMMU_BUS"},
		{GUID_BUS_INTERFACE_STANDARD, "BUS_INTERFACE_STANDARD"},
		{GUID_USBCAMD_INTERFACE, "USBCAMD"},
		{USB_BUS_INTERFACE_USBC_CONFIGURATION_GUID, "USB_BUS_INTERFACE_USBC_CONFIGURATION"},
		{KSMEDIUMSETID_Standard, "KSMEDIUMSETID_Standard"},
	};

	for (auto &i: v) {
		if (guid == i.guid) {
			return i.name;
		}
	} 
	
	return nullptr;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto query_interface(_Inout_ filter_ext &fltr, _In_ IRP *irp, _In_ const QueryInterface &qi)
{
	PAGED_CODE();

	if (auto ok = qi.InterfaceType && qi.Interface && qi.Size >= sizeof(*qi.Interface); !ok) {
		return CompleteRequest(irp, STATUS_INVALID_PARAMETER);
	}

	auto st = ForwardIrpSynchronously(fltr, irp);

	if (auto name = get_guid_name(*qi.InterfaceType); !NT_SUCCESS(st)) {
		if (name) {
			Trace(TRACE_LEVEL_ERROR, "dev %04x, %s, Size %d, Version %d, %!STATUS!", 
				ptr04x(fltr.self), name, qi.Size, qi.Version, st);
		} else {
			Trace(TRACE_LEVEL_ERROR, "dev %04x, %!GUID!, Size %d, Version %d, %!STATUS!", 
				ptr04x(fltr.self), qi.InterfaceType, qi.Size, qi.Version, st);
		}
	} else if (*qi.InterfaceType == USB_BUS_INTERFACE_USBDI_GUID) {
		auto &v3 = *reinterpret_cast<USB_BUS_INTERFACE_USBDI_V3*>(qi.Interface); // highest

		TraceDbg("dev %04x, USB_BUS_INTERFACE_USBDI_GUID, Size %d, Version %d", 
			  ptr04x(fltr.self), qi.Size, qi.Version);

		st = query_interface(fltr, v3);

	} else if (auto &i = *qi.Interface; name) {
		TraceDbg("dev %04x, %s, Size %d, Version %d -> Size %d, Version %d", 
			  ptr04x(fltr.self), name, qi.Size, qi.Version, i.Size, i.Version);
	} else {
		TraceDbg("dev %04x, %!GUID!, Size %d, Version %d -> Size %d, Version %d", 
			  ptr04x(fltr.self), qi.InterfaceType, qi.Size, qi.Version, i.Size, i.Version);
	}

	return CompleteRequest(irp, st);
}

} // namespace


/*
 * The remove-lock exists here to solve one problem:
 * prevent `fltr` (and the device stack it points to) from being torn down
 * by `IRP_MN_REMOVE_DEVICE` while this driver is still in the middle of using it:
 *
 *  - IoAcquireRemoveLock marks "I'm actively touching `fltr` right now
 *  - IRP_MN_REMOVE_DEVICE's handler calls IoReleaseRemoveLockAndWait, which blocks until
 *    every outstanding acquirer has released — guaranteeing no thread is still reading/writing `fltr`
 *    when `destroy(fltr)` runs and frees the device object.
 *  - Without it, a PnP-triggered removal could free `fltr`/`fltr.self` while another thread
 *    (e.g. an in-flight dispatch or completion routine) is still dereferencing it — a use-after-free.
 *
 * So each dispatch routine acquires the lock before touching `fltr`, and releases it (directly,
 * or by handing it off to a completion routine via `adopt_lock`) as soon as it's done touching `fltr`
 * for that IRP — which is exactly why the fall-through case in `pnp()` is safe releasing early:
 * its last touch of `fltr` is the `IoCallDriver` call itself.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
PAGED NTSTATUS usbip::pnp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	auto &fltr = *get_filter_ext(devobj);

	remove_lock_guard lck(fltr.remove_lock, irp);
	if (!lck) {
		auto err = lck.status();
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

	switch (auto &stack = *IoGetCurrentIrpStackLocation(irp); stack.MinorFunction) {
	case IRP_MN_START_DEVICE: { // must be started after lower device objects
		auto st = ForwardIrpSynchronously(fltr, irp);
		return CompleteRequest(irp, st);
	}
	case IRP_MN_REMOVE_DEVICE:
		return remove_device(fltr, irp, lck);
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		if (fltr.is_hub && stack.Parameters.QueryDeviceRelations.Type == BusRelations) {
			return query_bus_relations(fltr, irp);
		}
		break;
	case IRP_MN_QUERY_INTERFACE:
		if (auto &qi = stack.Parameters.QueryInterface; !fltr.is_hub) {
			return ::query_interface(fltr, irp, qi);
		}
		break;
	}

	return ForwardIrp(fltr, irp);
}
