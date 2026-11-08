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
 * In standard USB/WDK architecture, QueryBusTime returns the current 32-bit USB frame
 * number (1 frame = 1 ms) from the host controller's hardware Start-of-Frame (SOF) counter.
 *
 * Isochronous class drivers (such as usbaudio.sys) use this value to calculate current stream
 * position and packet latency:
 *     latency = CurrentUsbFrame - URB.StartFrame
 *
 * In USB/IP, URB.StartFrame is populated from the remote server's
 * usbip_header_ret_submit.start_frame (@see isoch_transfer in drivers/ude/wsk_receive.cpp).
 * Servers like usbipd-win (WinUSB/UsbDk backend) do not track hardware frame numbers and
 * always report start_frame = 0, while Linux servers use their own host controller's epoch.
 *
 * If this function calculates a real-time clock from client uptime (e.g. KeQueryInterruptTime() / 10'000),
 * the calculated latency becomes the client uptime (days / billions of ms) instead of a few ms.
 * This huge offset causes usbaudio.sys to treat the stream position as out-of-bounds, failing
 * capture (e.g. Sound Recorder producing 0-second recordings).
 *
 * Therefore, we calculate and return a monotonic counter rather than a real-time clock.
 * This keeps the epoch aligned near URB.StartFrame and allows audio capture to function properly.
 *
 * @return the current 32-bit USB frame number
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTime(
	_In_opt_ void*, // BusContext
	_Out_opt_ ULONG *CurrentUsbFrame)
{
	if (!CurrentUsbFrame) { // like usbport.sys and Microsoft/ReactOS ucx01000.sys do
		return STATUS_INVALID_PARAMETER;
	}

	static LONG frame;
	*CurrentUsbFrame = InterlockedIncrement(&frame);

	return STATUS_SUCCESS;
}

/*
 * In standard USB/WDK architecture, QueryBusTimeEx returns the current USB frame and micro-frame
 * number (1 micro-frame = 125 us, 8 micro-frames per 1 ms frame).
 * The lowest 3 bits contain the current 125 us micro-frame (0..7), while the upper 29 bits
 * contain the current 1 ms USB frame number.
 *
 * Since we return a synthesized counter for QueryBusTime, micro-frame bits (0..2) are kept 0
 * and the upper 29 bits are shifted from the QueryBusTime counter (*HighSpeedFrameCounter <<= 3).
 *
 * @return the current USB 2.0 frame/micro-frame counter
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME_EX)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTimeEx(
	_In_opt_ void *BusContext,
	_Out_opt_ ULONG *HighSpeedFrameCounter)
{
	if (!HighSpeedFrameCounter) {
		return STATUS_INVALID_PARAMETER;
	}

	auto st = QueryBusTime(BusContext, HighSpeedFrameCounter);
	if (NT_SUCCESS(st)) {
		*HighSpeedFrameCounter <<= 3; // 8 micro-frames per 1ms frame
	}
	return st;
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
		} else if (auto st = r.QueryBusTimeEx(r.BusContext, &dummy); !NT_SUCCESS(st)) {
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
		} else if (auto st = r.QueryBusTime(r.BusContext, &dummy); !NT_SUCCESS(st)) {
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
