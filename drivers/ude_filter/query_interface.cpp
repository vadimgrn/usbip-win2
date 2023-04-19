/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "query_interface.h"
#include "trace.h"
#include "query_interface.tmh"

#include <usb.h>
#include <usbbusif.h>

namespace
{

/*
 * @return the current 32-bit USB frame number
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTime(
	_In_ void *, 
	_Out_opt_ ULONG *CurrentUsbFrame)
{
	if (CurrentUsbFrame) {
		static ULONG dummy;
		*CurrentUsbFrame = ++dummy; // zero is OK too
		// TraceDbg("%lu", *CurrentUsbFrame); // too often
	}

	return STATUS_SUCCESS;
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
		// TraceDbg("%lu", **HighSpeedFrameCounter); // too often
	}
	return st;
}

} // namespace


/*
 * Audio devices do not work if QueryBusTime returns an error.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::query_interface(_Inout_ filter_ext &, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();

	switch (ULONG dummy; r.Version) {
	case USB_BUSIF_USBDI_VERSION_3:
		if (auto st = r.QueryBusTimeEx(r.BusContext, &dummy); NT_ERROR(st)) {
			TraceDbg("QueryBusTimeEx -> %!STATUS!, substituted", st);
			r.QueryBusTimeEx = QueryBusTimeEx;
		}
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_2:
	case USB_BUSIF_USBDI_VERSION_1:
	case USB_BUSIF_USBDI_VERSION_0:
		if (auto st = r.QueryBusTime(r.BusContext, &dummy); NT_ERROR(st)) {
			TraceDbg("QueryBusTime -> %!STATUS!, substituted", st);
			r.QueryBusTime = QueryBusTime;
		}
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unexpected USB_BUSIF_USBDI_VERSION_%lu", r.Version);
	}
}
