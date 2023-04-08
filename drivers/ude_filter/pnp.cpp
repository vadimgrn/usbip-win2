/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pnp.h"
#include "trace.h"
#include "pnp.tmh"

#include "driver.h"
#include "irp.h"

#include <libdrv\remove_lock.h>
#include <libdrv\ioctl.h>

#include <usbbusif.h>

namespace
{

using namespace usbip;

constexpr auto SizeOf_DEVICE_RELATIONS(_In_ ULONG cnt)
{
	return sizeof(DEVICE_RELATIONS) + (cnt ? --cnt*sizeof(*DEVICE_RELATIONS::Objects) : 0);
}
static_assert(SizeOf_DEVICE_RELATIONS(0) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(1) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(2)  > sizeof(DEVICE_RELATIONS));

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto clone(_In_ const DEVICE_RELATIONS &src)
{
	PAGED_CODE();

	auto sz = SizeOf_DEVICE_RELATIONS(src.Count);
	auto dst = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, sz, pooltag);

	if (dst) {
		RtlCopyMemory(dst, &src, sz);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
	}

	return dst;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
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

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED void query_bus_relations(_Inout_ filter_ext &fltr, _In_ const DEVICE_RELATIONS &r)
{
	PAGED_CODE();

	NT_ASSERT(fltr.is_hub);
	auto &previous = fltr.hub.previous;

	for (ULONG i = 0; i < r.Count; ++i) {
		auto pdo = r.Objects[i];
		if (!(previous && contains(*previous, pdo))) {
			TraceDbg("Creating a FiDO for PDO %04x", ptr04x(pdo));
			do_add_device(fltr.self->DriverObject, pdo, &fltr);
		}
	}

	auto assign = [] (auto &ptr, auto addr) 
	{
		if (ptr) {
			ExFreePoolWithTag(ptr, pooltag);
		}
		ptr = addr;
	};

	if (!r.Count) {
		assign(previous, nullptr);
	} else if (auto ptr = clone(r)) { // leave as is in case of an error
		assign(previous, ptr);
	}
}

/*
 * After we forward the request, the bus driver have created or deleted
 * a child device object. When bus driver created one (or more), this is the PDO
 * of our target device, we create and attach a filter object to it.
 * Note that we only attach the last detected USB device on it's hub.
 */
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
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

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto remove_device(_Inout_ filter_ext &fltr, _In_ IRP *irp, _In_ libdrv::RemoveLockGuard &lock)
{
	PAGED_CODE();
	Trace(TRACE_LEVEL_INFORMATION, "%04x", ptr04x(fltr.self));

	if (auto &handle = fltr.dev.usbd) {
		USBD_CloseHandle(handle); // must be called before sending the IRP down the USB driver stack
		handle = nullptr;
	}

	auto st = ForwardIrp(fltr, irp); // drivers must not fail this IRP

	lock.release_and_wait();
	destroy(fltr);

	return st;
}

/*
 * If return zero, QueryBusTime() will be called again and again.
 * @return the current 32-bit USB frame number
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME)
NTSTATUS USB_BUSIFFN QueryBusTime(_In_ void*, _Out_opt_ ULONG *CurrentUsbFrame)
{
	if (CurrentUsbFrame) {
		*CurrentUsbFrame = 0;
//		TraceDbg("%lu", *CurrentUsbFrame); // too often
		return STATUS_SUCCESS;
	}

	return STATUS_INVALID_PARAMETER;
}

/*
 * @return the current USB 2.0 frame/micro-frame number when called for
 *         a USB device attached to a USB 2.0 host controller
 *
 * The lowest 3 bits of the returned micro-frame value will contain the current 125us
 * micro-frame, while the upper 29 bits will contain the current 1ms USB frame number.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME_EX)
NTSTATUS USB_BUSIFFN QueryBusTimeEx(_In_opt_ void *BusContext, _Out_opt_ ULONG *HighSpeedFrameCounter)
{
	auto st = QueryBusTime(BusContext, HighSpeedFrameCounter);
	if (NT_SUCCESS(st)) {
		*HighSpeedFrameCounter <<= 3;
	}
	return st;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED auto query_interface_usbdi(_Inout_ filter_ext &fltr, _In_ IRP *irp, _Inout_ _IO_STACK_LOCATION &stack)
{
	PAGED_CODE();

	auto &qi = stack.Parameters.QueryInterface;
	auto &v3 = *reinterpret_cast<USB_BUS_INTERFACE_USBDI_V3*>(qi.Interface); // highest

	auto st = ForwardIrpSynchronously(fltr, irp);

	if (NT_SUCCESS(st)) {
		TraceDbg("Size %d, Version %d => Size %d, Version %d", qi.Size, qi.Version, v3.Size, v3.Version);

		switch (ULONG frame; v3.Version) {
		case USB_BUSIF_USBDI_VERSION_3:
			if (v3.QueryBusTimeEx(v3.BusContext, &frame) == STATUS_NOT_SUPPORTED) {
				v3.QueryBusTimeEx = QueryBusTimeEx;
			}
			[[fallthrough]];
		default:
			if (v3.QueryBusTime(v3.BusContext, &frame) == STATUS_NOT_SUPPORTED) {
				v3.QueryBusTime = QueryBusTime;
			}
		}
	}

	CompleteRequest(irp);
	return st;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
PAGED NTSTATUS usbip::pnp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	auto &fltr = *get_filter_ext(devobj);

	libdrv::RemoveLockGuard lck(fltr.remove_lock);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

	switch (auto &stack = *IoGetCurrentIrpStackLocation(irp); stack.MinorFunction) {
	case IRP_MN_START_DEVICE: // must be started after lower device objects
		if constexpr (true) {
			auto st = ForwardIrpSynchronously(fltr, irp); 
			CompleteRequest(irp);
			return st;
		}
	case IRP_MN_REMOVE_DEVICE:
		return remove_device(fltr, irp, lck);
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		if (fltr.is_hub && stack.Parameters.QueryDeviceRelations.Type == BusRelations) {
			return query_bus_relations(fltr, irp);
		}
		break;
	case IRP_MN_QUERY_INTERFACE:
		if (auto &qi = stack.Parameters.QueryInterface; 
		    !fltr.is_hub && IsEqualGUID(*qi.InterfaceType, USB_BUS_INTERFACE_USBDI_GUID)) {
			return query_interface_usbdi(fltr, irp, stack);
		}
		break;
	}

	return ForwardIrp(fltr, irp);
}
