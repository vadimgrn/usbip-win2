#include "vhci_urbr_fetch_iso.h"
#include "vhci_driver.h"
#include "vhci_urbr_fetch_iso.tmh"

#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "usbd_helper.h"

static BOOLEAN
save_iso_desc(struct _URB_ISOCH_TRANSFER *urb, struct usbip_iso_packet_descriptor *iso_desc)
{
	for (ULONG i = 0; i < urb->NumberOfPackets; ++i) {
		if (iso_desc->offset > urb->IsoPacket[i].Offset) {
			TRW(WRITE, "why offset changed?%d %d %d %d", i, iso_desc->offset, iso_desc->actual_length, urb->IsoPacket[i].Offset);
			return FALSE;
		}
		urb->IsoPacket[i].Length = iso_desc->actual_length;
		urb->IsoPacket[i].Status = to_usbd_status(iso_desc->status);
		iso_desc++;
	}

	return TRUE;
}

static void
fetch_iso_data(char *dest, ULONG dest_len, char *src, ULONG src_len, struct _URB_ISOCH_TRANSFER *urb)
{
	ULONG	offset = 0;

	for (ULONG i = 0; i < urb->NumberOfPackets; i++) {
		if (urb->IsoPacket[i].Length == 0)
			continue;

		if (urb->IsoPacket[i].Offset + urb->IsoPacket[i].Length > dest_len) {
			TRW(WRITE, "Warning, why this?");
			break;
		}
		if (offset + urb->IsoPacket[i].Length > src_len) {
			TRE(WRITE, "Warning, why that?");
			break;
		}
		RtlCopyMemory(dest + urb->IsoPacket[i].Offset, src + offset, urb->IsoPacket[i].Length);
		offset += urb->IsoPacket[i].Length;
	}

	if (offset != src_len) {
		TRW(WRITE, "why not equal offset:%d src_len:%d", offset, src_len);
	}
}

NTSTATUS
fetch_urbr_iso(PURB urb, struct usbip_header *hdr)
{
	struct _URB_ISOCH_TRANSFER *urb_iso = &urb->UrbIsochronousTransfer;

	size_t in_len = IsTransferDirectionIn(urb_iso->TransferFlags) ? hdr->u.ret_submit.actual_length : 0;
	struct usbip_iso_packet_descriptor *iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + in_len);
	
	if (!save_iso_desc(urb_iso, iso_desc)) {
		return STATUS_INVALID_PARAMETER;
	}

	urb_iso->ErrorCount = hdr->u.ret_submit.error_count;
	/* The RET_SUBMIT packets of the OUT ISO transfer do not contain data buffer. */
	if (IsTransferDirectionOut(urb_iso->TransferFlags)) {
		return STATUS_SUCCESS;
	}

	PVOID buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
	if (buf == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	fetch_iso_data(buf, urb_iso->TransferBufferLength, (char *)(hdr + 1), hdr->u.ret_submit.actual_length, urb_iso);
	urb_iso->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return STATUS_SUCCESS;
}
