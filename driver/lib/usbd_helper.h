#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "usbip_proto.h"
#include "ch9.h"

#include <stdbool.h>

#include <ntddk.h>
#include <usbdi.h>

struct usbip_iso_packet_descriptor;

USBD_STATUS to_windows_status(int usbip_status);
int to_linux_status(USBD_STATUS usbd_status);

ULONG to_windows_flags(UINT32 transfer_flags, bool dir_in);
UINT32 to_linux_flags(ULONG TransferFlags, bool dir_in);

void to_usbd_iso_descs(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs,
	const struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN as_result);

void to_iso_descs(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs, BOOLEAN as_result);

ULONG get_iso_descs_len(ULONG n_pkts, const struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN is_actual);
ULONG get_usbd_iso_descs_len(ULONG n_pkts, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs);

__inline bool IsTransferDirectionIn(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_IN;
}

__inline bool IsTransferDirectionOut(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT;
}

__inline bool is_transfer_dir_in(const struct _URB_CONTROL_TRANSFER *r)
{
	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = (USB_DEFAULT_PIPE_SETUP_PACKET*)r->SetupPacket;

	static_assert(USB_DIR_IN, "assert");
	return pkt->bmRequestType.B & USB_DIR_IN; // C: bmRequestType.Dir, C++: bmRequestType.s.Dir
}

__inline bool is_transfer_dir_out(const struct _URB_CONTROL_TRANSFER *r)
{
	static_assert(!USB_DIR_OUT, "assert");
	return !is_transfer_dir_in(r);
}

__inline bool is_transfer_direction_in(const struct usbip_header *h)
{
	return h->base.direction == USBIP_DIR_IN;
}

__inline bool is_transfer_direction_out(const struct usbip_header *h)
{
	return h->base.direction == USBIP_DIR_OUT;
}

#ifdef __cplusplus
}
#endif
