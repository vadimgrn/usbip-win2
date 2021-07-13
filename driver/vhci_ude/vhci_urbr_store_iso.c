#include "vhci_urbr_store_iso.h"
#include "vhci_driver.h"
#include "vhci_urbr_store_iso.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

static NTSTATUS
store_iso_data(PVOID dst, struct _URB_ISOCH_TRANSFER *urb_iso)
{
	struct usbip_iso_packet_descriptor	*iso_desc;
	char	*buf;
	ULONG	i, offset;

	buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	if (IS_TRANSFER_FLAGS_IN(urb_iso->TransferFlags)) {
		iso_desc = (struct usbip_iso_packet_descriptor *)dst;
	}
	else {
		RtlCopyMemory(dst, buf, urb_iso->TransferBufferLength);
		iso_desc = (struct usbip_iso_packet_descriptor *)((char *)dst + urb_iso->TransferBufferLength);
	}

	offset = 0;
	for (i = 0; i < urb_iso->NumberOfPackets; i++) {
		if (urb_iso->IsoPacket[i].Offset < offset) {
			TRW(READ, "strange iso packet offset:%d %d", offset, urb_iso->IsoPacket[i].Offset);
			return STATUS_INVALID_PARAMETER;
		}
		iso_desc->offset = urb_iso->IsoPacket[i].Offset;
		if (i > 0)
			(iso_desc - 1)->length = urb_iso->IsoPacket[i].Offset - offset;
		offset = urb_iso->IsoPacket[i].Offset;
		iso_desc->actual_length = 0;
		iso_desc->status = 0;
		iso_desc++;
	}
	(iso_desc - 1)->length = urb_iso->TransferBufferLength - offset;

	return STATUS_SUCCESS;
}

static ULONG
get_iso_payload_len(struct _URB_ISOCH_TRANSFER *urb_iso)
{
	ULONG	len_iso;

	len_iso = urb_iso->NumberOfPackets * sizeof(struct usbip_iso_packet_descriptor);
	if (!IS_TRANSFER_FLAGS_IN(urb_iso->TransferFlags)) {
		len_iso += urb_iso->TransferBufferLength;
	}
	return len_iso;
}

NTSTATUS
store_urbr_iso_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_ISOCH_TRANSFER	*urb_iso = &urbr->u.urb.urb->UrbIsochronousTransfer;
	ULONG	len_iso;
	PVOID	dst;

	len_iso = get_iso_payload_len(urb_iso);

	dst = get_data_from_req_read(req_read, len_iso);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	store_iso_data(dst, urb_iso);
	urbr->ep->vusb->len_sent_partial = 0;
	WdfRequestSetInformation(req_read, len_iso);

	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_iso(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_ISOCH_TRANSFER	*urb_iso = &urbr->u.urb.urb->UrbIsochronousTransfer;
	struct usbip_header	*hdr;
	int	in, type;

	in = IS_TRANSFER_FLAGS_IN(urb_iso->TransferFlags);
	type = urbr->ep->type;
	if (type != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
		TRE(READ, "Error, not a iso pipe");
		return STATUS_INVALID_PARAMETER;
	}

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, in, urbr->ep,
		urb_iso->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_iso->TransferBufferLength);
	hdr->u.cmd_submit.start_frame = urb_iso->StartFrame;
	hdr->u.cmd_submit.number_of_packets = urb_iso->NumberOfPackets;

	if (get_read_payload_length(req_read) >= get_iso_payload_len(urb_iso)) {
		store_iso_data(hdr + 1, urb_iso);
		WdfRequestSetInformation(req_read, sizeof(struct usbip_header) + get_iso_payload_len(urb_iso));
	}
	else {
		WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
		urbr->ep->vusb->len_sent_partial = sizeof(struct usbip_header);
	}

	return STATUS_SUCCESS;
}
