#include "pdu.h"

#include <usb.h>
#include <usbdi.h>

/*
 * Linux error codes.
 * See: include/uapi/asm-generic/errno-base.h, include/uapi/asm-generic/errno.h
 */
enum { 
	EPIPE_LINUX = 32, 
	EOVERFLOW_LINUX = 75, 
	EREMOTEIO_LINUX = 121 
};

USBD_STATUS
to_usbd_status(int usbip_status)
{
	switch (usbip_status) {
	case 0:
		return USBD_STATUS_SUCCESS;
		/* I guess it */
	case -EPIPE_LINUX:
		return USBD_STATUS_STALL_PID;
	case -EOVERFLOW_LINUX:
		return USBD_STATUS_DATA_OVERRUN;
	case -EREMOTEIO_LINUX:
		return USBD_STATUS_ERROR_SHORT_TRANSFER;
	default:
		return USBD_STATUS_ERROR;
	}
}

int
to_usbip_status(USBD_STATUS status)
{
	switch (status) {
	case 0:
		return 0;
	case USBD_STATUS_STALL_PID:
		return -EPIPE;
	default:
		return -1;
	}
}

/*
 * include/linux/usb.h
 */
enum {
	URB_SHORT_NOT_OK = 0x0001,
	URB_ISO_ASAP = 0x0002,
	URB_DIR_IN = 0x0200
};

ULONG
to_usbd_flags(int flags)
{
	ULONG usbd_flags = 0;

	if (flags & URB_SHORT_NOT_OK)
		usbd_flags |= USBD_SHORT_TRANSFER_OK;
	if (flags & URB_ISO_ASAP)
		usbd_flags |= USBD_START_ISO_TRANSFER_ASAP;
	if (flags & URB_DIR_IN)
		usbd_flags |= USBD_TRANSFER_DIRECTION_IN;
	return usbd_flags;
}

void
to_usbd_iso_descs(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs, struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN as_result)
{
	USBD_ISO_PACKET_DESCRIPTOR	*usbd_iso_desc;
	struct usbip_iso_packet_descriptor	*iso_desc;
	ULONG	i;

	usbd_iso_desc = usbd_iso_descs;
	iso_desc = iso_descs;
	for (i = 0; i < n_pkts; i++) {
		usbd_iso_desc->Offset = iso_desc->offset;
		if (as_result) {
			usbd_iso_desc->Length = iso_desc->actual_length;
			usbd_iso_desc->Status = to_usbd_status(iso_desc->status);
		}
		usbd_iso_desc++;
		iso_desc++;
	}
}

void
to_iso_descs(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs, BOOLEAN as_result)
{
	USBD_ISO_PACKET_DESCRIPTOR	*usbd_iso_desc;
	struct usbip_iso_packet_descriptor	*iso_desc;
	ULONG	i;

	iso_desc = iso_descs;
	usbd_iso_desc = usbd_iso_descs;
	for (i = 0; i < n_pkts; i++) {
		iso_desc->offset = usbd_iso_desc->Offset;
		if (as_result) {
			iso_desc->actual_length = usbd_iso_desc->Length;
			iso_desc->status = to_usbip_status(usbd_iso_desc->Status);
		}
		usbd_iso_desc++;
		iso_desc++;
	}
}

ULONG
get_iso_descs_len(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN is_actual)
{
	ULONG	len = 0;
	struct usbip_iso_packet_descriptor	*iso_desc = iso_descs;
	ULONG	i;

	for (i = 0; i < n_pkts; i++) {
		len += (is_actual ? iso_desc->actual_length: iso_desc->length);
		iso_desc++;
	}
	return len;
}

ULONG
get_usbd_iso_descs_len(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs)
{
	ULONG	len = 0;
	USBD_ISO_PACKET_DESCRIPTOR	*usbd_iso_desc = usbd_iso_descs;
	ULONG	i;

	for (i = 0; i < n_pkts; i++) {
		len += usbd_iso_desc->Length;
		usbd_iso_desc++;
	}
	return len;
}