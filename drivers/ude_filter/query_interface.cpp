/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
	_In_opt_ void *, 
	_Out_opt_ ULONG *CurrentUsbFrame)
{
	if (CurrentUsbFrame) {
		// 1 frame = 1ms = 10,000 * 100ns units
		*CurrentUsbFrame = static_cast<ULONG>(KeQueryInterruptTime() / 10'000);
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
	_In_opt_ void *,
	_Out_opt_ ULONG *HighSpeedFrameCounter)
{
	if (HighSpeedFrameCounter) {
		// 1 micro-frame = 125us = 1,250 * 100ns units (8 micro-frames per 1ms frame)
		*HighSpeedFrameCounter = static_cast<ULONG>(KeQueryInterruptTime() / 1'250);
	}

	return STATUS_SUCCESS;
}

} // namespace


/*
 * Audio devices do not work if QueryBusTime returns an error.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::query_interface(_Inout_ [[maybe_unused]] filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();

	const auto end = offsetof(_USB_BUS_INTERFACE_USBDI_V3, QueryBusTime) + sizeof(r.QueryBusTime);
	const auto ex_end = offsetof(_USB_BUS_INTERFACE_USBDI_V3, QueryBusTimeEx) + sizeof(r.QueryBusTimeEx);

	if (r.Size < end) {
		return STATUS_INVALID_PARAMETER;
	}

	switch (ULONG dummy; r.Version) {
	case USB_BUSIF_USBDI_VERSION_3:
		if (r.Size < ex_end) {
			return STATUS_INVALID_PARAMETER;
		}
		if (!r.QueryBusTimeEx) {
			TraceDbg("QueryBusTimeEx is NULL, substituted");
			r.QueryBusTimeEx = QueryBusTimeEx;
		} else if (auto st = r.QueryBusTimeEx(r.BusContext, &dummy); NT_ERROR(st)) {
			TraceDbg("QueryBusTimeEx -> %!STATUS!, substituted", st);
			r.QueryBusTimeEx = QueryBusTimeEx;
		}
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_2:
	case USB_BUSIF_USBDI_VERSION_1:
	case USB_BUSIF_USBDI_VERSION_0:
		if (!r.QueryBusTime) {
			TraceDbg("QueryBusTime is NULL, substituted");
			r.QueryBusTime = QueryBusTime;
		} else if (auto st = r.QueryBusTime(r.BusContext, &dummy); NT_ERROR(st)) {
			TraceDbg("QueryBusTime -> %!STATUS!, substituted", st);
			r.QueryBusTime = QueryBusTime;
		}
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "Unexpected USB_BUSIF_USBDI_VERSION_%lu", r.Version);
		break;
	}

	return STATUS_SUCCESS;
}
