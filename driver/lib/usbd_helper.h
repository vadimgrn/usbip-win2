#pragma once

#include "usbip_proto.h"
#include "ch9.h"

#include <stdbool.h>

#include <ntddk.h>
#include <usbdi.h>

struct usbip_iso_packet_descriptor;

enum { EndpointStalled = USBD_STATUS_STALL_PID }; // FIXME: for what USBD_STATUS_ENDPOINT_HALTED?

int to_linux_status(USBD_STATUS usbd_status);
USBD_STATUS to_windows_status_ex(int usbip_status, bool isoch);

inline USBD_STATUS to_windows_status(int usbip_status) { return to_windows_status_ex(usbip_status, false); }
inline USBD_STATUS to_windows_status_isoch(int usbip_status) { return to_windows_status_ex(usbip_status, true); }

ULONG to_windows_flags(UINT32 transfer_flags, bool dir_in);
UINT32 to_linux_flags(ULONG TransferFlags, bool dir_in);

inline bool IsTransferDirectionIn(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_IN;
}

inline bool IsTransferDirectionOut(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT;
}

inline bool is_transfer_dir_in(const struct _URB_CONTROL_TRANSFER *r)
{
	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = (USB_DEFAULT_PIPE_SETUP_PACKET*)r->SetupPacket;

	static_assert(USB_DIR_IN, "assert");
	return pkt->bmRequestType.B & USB_DIR_IN; // C: bmRequestType.Dir, C++: bmRequestType.s.Dir
}

inline bool is_transfer_dir_out(const struct _URB_CONTROL_TRANSFER *r)
{
	static_assert(!USB_DIR_OUT, "assert");
	return !is_transfer_dir_in(r);
}

inline bool is_transfer_direction_in(const struct usbip_header *h)
{
	return h->base.direction == USBIP_DIR_IN;
}

inline bool is_transfer_direction_out(const struct usbip_header *h)
{
	return h->base.direction == USBIP_DIR_OUT;
}
