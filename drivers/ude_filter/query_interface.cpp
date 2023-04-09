/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "query_interface.h"
#include "trace.h"
#include "query_interface.tmh"

#include "driver.h"
#include "device.h"
#include <usbbusif.h>

namespace
{

using namespace usbip;

struct context
{
	LONG ref_count;
	filter_ext *self;
	_USB_BUS_INTERFACE_USBDI_V3 v3;
};

inline auto get_context(_In_ void *BusContext)
{
	return static_cast<context*>(BusContext);
}

inline auto& get_interface(_In_ void *BusContext)
{
	return get_context(BusContext)->v3;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto alloc_context(_In_ filter_ext &fltr, _In_ const _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	context *ctx = (context*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ctx), pooltag);

	if (ctx) {
		ctx->ref_count = 1;
		ctx->self = &fltr;
		RtlCopyMemory(&ctx->v3, &r, min(r.Size, sizeof(r)));
		TraceDbg("%04x", ptr04x(ctx));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Cannot allocate %Iu bytes", sizeof(*ctx));
	}

	return ctx;
}

_Function_class_(PINTERFACE_REFERENCE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN InterfaceReference(_In_ void *BusContext)
{
	auto ctx = get_context(BusContext);

	auto &i = ctx->v3;
	i.InterfaceReference(i.BusContext);

	auto cnt = InterlockedIncrement(&ctx->ref_count); 
	TraceDbg("%04x, ref_count %lu", ptr04x(ctx), cnt);
	NT_VERIFY(cnt > 0);
}

_Function_class_(PINTERFACE_DEREFERENCE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN InterfaceDereference(_In_ void *BusContext)
{
	auto ctx = get_context(BusContext);

	auto &i = ctx->v3;
	i.InterfaceDereference(i.BusContext);

	auto cnt = InterlockedDecrement(&ctx->ref_count);
	TraceDbg("%04x, ref_count %lu", ptr04x(ctx), cnt);

	if (!cnt) {
		ExFreePoolWithTag(ctx, pooltag);
	} else {
		NT_ASSERT(cnt > 0);
	}
}

_Function_class_(PUSB_BUSIFFN_GETUSBDI_VERSION)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN GetUSBDIVersion(
	_In_ void *BusContext,
	_Out_opt_ PUSBD_VERSION_INFORMATION a,
	_Out_opt_ PULONG b)
{
	auto &i = get_interface(BusContext);
	i.GetUSBDIVersion(i.BusContext, a, b);
}

/*
 * If return zero, QueryBusTime() will be called again and again.
 * @return the current 32-bit USB frame number
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTime(
	_In_ void *BusContext, 
	_Out_opt_ ULONG *CurrentUsbFrame)
{
	if (!CurrentUsbFrame) {
		return STATUS_INVALID_PARAMETER;
	}
	
	auto fltr = get_context(BusContext)->self;
	auto &val = *CurrentUsbFrame;

	val = fltr->dev.current_frame_number;
	if (!val) {
		val = USBD_ISO_START_FRAME_RANGE/2;
	}

	return STATUS_SUCCESS;
}

_Function_class_(PUSB_BUSIFFN_SUBMIT_ISO_OUT_URB)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN SubmitIsoOutUrb(_In_ void *BusContext, _In_ URB *urb)
{
	auto &i = get_interface(BusContext);
	return i.SubmitIsoOutUrb(i.BusContext, urb);
}

_Function_class_(PUSB_BUSIFFN_QUERY_BUS_INFORMATION)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusInformation(
	_In_ void *BusContext,
	_In_ ULONG a,
	_Inout_ PVOID b,
	_Out_ PULONG c,
	_Out_opt_ PULONG d)
{
	auto &i = get_interface(BusContext);
	return i.QueryBusInformation(i.BusContext, a, b, c, d);
}

_Function_class_(PUSB_BUSIFFN_IS_DEVICE_HIGH_SPEED)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ BOOLEAN USB_BUSIFFN IsDeviceHighSpeed(_In_opt_ void *BusContext)
{
	auto &i = get_interface(BusContext);
	return i.IsDeviceHighSpeed(i.BusContext);
}

_Function_class_(PUSB_BUSIFFN_ENUM_LOG_ENTRY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS USB_BUSIFFN EnumLogEntry(
	_In_ PVOID BusContext,
	_In_ ULONG a,
	_In_ ULONG b,
	_In_ ULONG c,
	_In_ ULONG d)
{
	auto &i = get_interface(BusContext);
	return i.EnumLogEntry(i.BusContext, a, b, c, d);
}

/*
 * @return the current USB 2.0 frame/micro-frame number when called for
 *         a USB device attached to a USB 2.0 host controller
 *
 * The lowest 3 bits of the returned micro-frame value will contain the current 125us
 * micro-frame, while the upper 29 bits will contain the current 1ms USB frame number.
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME_EX)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTimeEx(
	_In_opt_ PVOID BusContext,
	_Out_opt_ PULONG HighSpeedFrameCounter)
{
	auto st = QueryBusTime(BusContext, HighSpeedFrameCounter);
	if (NT_SUCCESS(st)) {
		*HighSpeedFrameCounter <<= 3;
	}

	return st;
}

_Function_class_(PUSB_BUSIFFN_QUERY_CONTROLLER_TYPE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryControllerType(
	_In_opt_ PVOID BusContext,
	_Out_opt_ PULONG a,
	_Out_opt_ PUSHORT b,
	_Out_opt_ PUSHORT c,
	_Out_opt_ PUCHAR d,
	_Out_opt_ PUCHAR e,
	_Out_opt_ PUCHAR f,
	_Out_opt_ PUCHAR g)
{
	auto &i = get_interface(BusContext);
	return i.QueryControllerType(i.BusContext, a, b, c, d, e, f, g);
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS usbip::replace_interface(_Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r, _In_ filter_ext &fltr)
{
	PAGED_CODE();

	auto ctx = alloc_context(fltr, r);
	if (!ctx) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	switch (r.Version) {
	case USB_BUSIF_USBDI_VERSION_3:
		r.QueryBusTimeEx = QueryBusTimeEx;
		r.QueryControllerType = QueryControllerType;
	case USB_BUSIF_USBDI_VERSION_2:
		r.EnumLogEntry = EnumLogEntry;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_1:
		r.IsDeviceHighSpeed = IsDeviceHighSpeed;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_0:
		r.GetUSBDIVersion = GetUSBDIVersion;
		r.QueryBusTime = QueryBusTime;
		r.SubmitIsoOutUrb = SubmitIsoOutUrb;
		r.QueryBusInformation = QueryBusInformation;
		[[fallthrough]];
	default: // INTERFACE
		r.BusContext = ctx;
		r.InterfaceReference = InterfaceReference;
		r.InterfaceDereference = InterfaceDereference;
	}

	return STATUS_SUCCESS;
}
