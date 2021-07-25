#pragma once

#include <stdbool.h>

#include <ntddk.h>
#include <usbdi.h>

struct usbip_iso_packet_descriptor;

USBD_STATUS to_usbd_status(int usbip_status);
int to_usbip_status(USBD_STATUS usbd_status);

ULONG to_windows_flags(UINT32 transfer_flags, bool dir_in);
UINT32 to_linux_flags(ULONG TransferFlags);

void to_usbd_iso_descs(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs,
	const struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN as_result);

void to_iso_descs(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs, BOOLEAN as_result);

ULONG get_iso_descs_len(ULONG n_pkts, const struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN is_actual);
ULONG get_usbd_iso_descs_len(ULONG n_pkts, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs);

enum { USB_REQUEST_RESET_PIPE = 0xfe };

__inline bool IsTransferDirectionIn(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_IN;
}

__inline bool IsTransferDirectionOut(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT;
}
