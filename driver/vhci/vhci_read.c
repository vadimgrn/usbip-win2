#include "vhci_read.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_read.tmh"

#include "vhci_irp.h"
#include "vhci_proto.h"
#include "vhci_internal_ioctl.h"
#include "usbd_helper.h"
#include "ch9.h"
#include "ch11.h"

static void *get_read_irp_data(IRP *irp, ULONG length)
{
	irp->IoStatus.Information = 0;
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	return irpstack->Parameters.Read.Length >= length ? irp->AssociatedIrp.SystemBuffer : NULL;
}

static __inline struct usbip_header *get_usbip_hdr_from_read_irp(IRP *irp)
{
	return get_read_irp_data(irp, sizeof(struct usbip_header));
}

static ULONG get_read_payload_length(IRP *irp)
{
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	return irpstack->Parameters.Read.Length - sizeof(struct usbip_header);
}

/*
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd.
 */
static NTSTATUS usb_reset_port(IRP *irp, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_RT_PORT; // USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER
	pkt->bRequest = USB_REQUEST_SET_FEATURE;
	pkt->wValue.W = USB_PORT_FEAT_RESET;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS get_descriptor_from_node_connection(IRP *irp, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	USB_DESCRIPTOR_REQUEST *r = urbr->irp->AssociatedIrp.SystemBuffer;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength - sizeof(*r);

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, outlen);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
	pkt->wValue.W = r->SetupPacket.wValue;
	pkt->wIndex.W = r->SetupPacket.wIndex;
	pkt->wLength = r->SetupPacket.wLength;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

/* 
 * Any URBs queued for such an endpoint should normally be unlinked by the driver before clearing the halt condition, 
 * as described in sections 5.7.5 and 5.8.5 of the USB 2.0 spec.
 * 
 * Thus, a driver must call URB_FUNCTION_ABORT_PIPE before URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL.
 * For that reason vhci_ioctl_abort_pipe(urbr->vpdo, r->PipeHandle) is not called here.
 * 
 * Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt which 
 * a) Issues USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT # URB_FUNCTION_SYNC_CLEAR_STALL
 * b) Calls usb_reset_endpoint # URB_FUNCTION_SYNC_RESET_PIPE
 * 
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
        <linux>/drivers/usb/core/message.c, usb_clear_halt
 */
static NTSTATUS sync_reset_pipe_and_clear_stall(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_PIPE_REQUEST *r = &urb->UrbPipeRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT;
	pkt->bRequest = USB_REQUEST_CLEAR_FEATURE;
	pkt->wValue.W = USB_FEATURE_ENDPOINT_STALL; // USB_ENDPOINT_HALT
	pkt->wIndex.W = get_endpoint_address(r->PipeHandle);

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static const void *get_buf(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}

	if (!bufMDL) {
		TraceError(TRACE_READ, "TransferBuffer and TransferBufferMDL are NULL");
		return NULL;
	}

	buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority | MdlMappingNoExecute | MdlMappingNoWrite);
	if (!buf) {
		TraceError(TRACE_READ, "MmGetSystemAddressForMdlSafe error");
	}

	return buf;
}

static __inline NTSTATUS ptr_to_status(const void *p)
{
	return p ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static NTSTATUS urb_control_descriptor_request(IRP *irp, URB *urb, struct urb_req *urbr, bool dir_in, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | 
					(dir_in ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, r->TransferBufferLength);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = (dir_in ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_STANDARD | recipient;
	pkt->bRequest = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
	pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r->DescriptorType, r->Index); 
	pkt->wIndex.W = r->LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
	pkt->wLength = (USHORT)r->TransferBufferLength;

	irp->IoStatus.Information = sizeof(*hdr);

	if (dir_in) {
		return STATUS_SUCCESS;
	}
	
	if (get_read_payload_length(irp) < r->TransferBufferLength) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	const void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(hdr + 1, buf, r->TransferBufferLength);
		irp->IoStatus.Information += r->TransferBufferLength;
	}

	return ptr_to_status(buf);
}

static NTSTATUS urb_control_get_status_request(IRP *irp, URB *urb, struct urb_req *urbr, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(TRACE_READ, "%s: %s", urb_function_str(urb->UrbHeader.Function), urb_req_str(buf, sizeof(buf), urbr));
	}

	struct _URB_CONTROL_GET_STATUS_REQUEST *r = &urb->UrbControlGetStatusRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, r->TransferBufferLength);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | recipient;
	pkt->bRequest = USB_REQUEST_GET_STATUS;
	pkt->wIndex.W = r->Index;
	pkt->wLength = (USHORT)r->TransferBufferLength; // must be 2
	
	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_vendor_class_request_partial(vpdo_dev_t *vpdo, IRP *irp, URB *urb)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;

	void *dst = get_read_irp_data(irp, r->TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	const void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(dst, buf, r->TransferBufferLength);
		irp->IoStatus.Information = r->TransferBufferLength;
		vpdo->len_sent_partial = 0;
	}

	return ptr_to_status(buf);
}

static NTSTATUS urb_control_vendor_class_request(IRP *irp, URB *urb, struct urb_req *urbr, UCHAR type, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;
	bool dir_in = IsTransferDirectionIn(r->TransferFlags);

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 
						EP0, r->TransferFlags | USBD_DEFAULT_PIPE_TRANSFER, r->TransferBufferLength);

	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = (dir_in ? USB_DIR_IN : USB_DIR_OUT) | type | recipient;
	pkt->bRequest = r->Request;
	pkt->wValue.W = r->Value;
	pkt->wIndex.W = r->Index;
	pkt->wLength = (USHORT)r->TransferBufferLength;

	irp->IoStatus.Information = sizeof(*hdr);

	if (dir_in) {
		return STATUS_SUCCESS;
	}

	if (get_read_payload_length(irp) < r->TransferBufferLength) {
		urbr->vpdo->len_sent_partial = sizeof(*hdr);
		return STATUS_SUCCESS;
	}
	
	const void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(hdr + 1, buf, r->TransferBufferLength);
		irp->IoStatus.Information += r->TransferBufferLength;
	}

	return ptr_to_status(buf);
}

static NTSTATUS vendor_device(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

static NTSTATUS vendor_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

static NTSTATUS vendor_endpoint(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

static NTSTATUS vendor_other(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

static NTSTATUS class_device(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

static NTSTATUS class_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

static NTSTATUS class_endpoint(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

static NTSTATUS class_other(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

static NTSTATUS urb_select_configuration(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_SELECT_CONFIGURATION *r = &urb->UrbSelectConfiguration;
	USB_CONFIGURATION_DESCRIPTOR *cd = r->ConfigurationDescriptor; // NULL if unconfigured

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
	pkt->wValue.W = cd ? cd->bConfigurationValue : 0;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS urb_select_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_SELECT_INTERFACE *r = &urb->UrbSelectInterface;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
	pkt->bRequest = USB_REQUEST_SET_INTERFACE;
	pkt->wValue.W = r->Interface.AlternateSetting;
	pkt->wIndex.W = r->Interface.InterfaceNumber;

	irp->IoStatus.Information = sizeof(*hdr);
	return  STATUS_SUCCESS;
}

static NTSTATUS urb_bulk_or_interrupt_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;

	void *dst = get_read_irp_data(irp, r->TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	const void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(dst, buf, r->TransferBufferLength);
		irp->IoStatus.Information = r->TransferBufferLength;
		vpdo->len_sent_partial = 0;
	}
	
	return ptr_to_status(buf);
}

static NTSTATUS urb_bulk_or_interrupt_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(r->PipeHandle);

	if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
		TraceError(TRACE_READ, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 
						r->PipeHandle, r->TransferFlags, r->TransferBufferLength);

	if (err) {
		return err;
	}

	irp->IoStatus.Information = sizeof(*hdr);

	if (IsTransferDirectionIn(r->TransferFlags)) {
		return STATUS_SUCCESS;
	}

	if (get_read_payload_length(irp) < r->TransferBufferLength) {
		urbr->vpdo->len_sent_partial = sizeof(*hdr);
		return STATUS_SUCCESS;
	}

	void *buf_a = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? NULL : r->TransferBuffer;
	
	const void *buf = get_buf(buf_a, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(hdr + 1, buf, r->TransferBufferLength);
	}

	return ptr_to_status(buf);
}

/*
 * USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
 */
static NTSTATUS copy_iso_data(void *dst_buf, const struct _URB_ISOCH_TRANSFER *r)
{
	void *buf_a = r->Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? NULL : r->TransferBuffer;

	const void *src_buf = get_buf(buf_a, r->TransferBufferMDL);
	if (!src_buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	bool dir_out = IsTransferDirectionOut(r->TransferFlags);
	ULONG buf_len = dir_out ? r->TransferBufferLength : 0;

	RtlCopyMemory(dst_buf, src_buf, buf_len);

	struct usbip_iso_packet_descriptor *d = (void*)((char*)dst_buf + buf_len);
	ULONG sum = 0;

	for (ULONG i = 0; i < r->NumberOfPackets; ++d) {

		ULONG offset = r->IsoPacket[i].Offset;
		ULONG next_offset = ++i < r->NumberOfPackets ? r->IsoPacket[i].Offset : r->TransferBufferLength;

		if (next_offset >= offset && next_offset <= r->TransferBufferLength) {
			d->offset = offset;
			d->length = next_offset - offset;
			d->actual_length = 0;
			d->status = 0;
			sum += d->length;
		} else {
			TraceError(TRACE_READ, "[%lu] next_offset(%lu) >= offset(%lu) && next_offset <= r->TransferBufferLength(%lu)",
						i, next_offset, offset, r->TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}
	}

	NT_ASSERT(sum == r->TransferBufferLength);
	return STATUS_SUCCESS;
}

static ULONG get_iso_payload_len(const struct _URB_ISOCH_TRANSFER *r)
{
	ULONG len = r->NumberOfPackets*sizeof(struct usbip_iso_packet_descriptor);
	
	if (IsTransferDirectionOut(r->TransferFlags)) {
		len += r->TransferBufferLength;
	}

	return len;
}

static NTSTATUS urb_isoch_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	const struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	ULONG len = get_iso_payload_len(r);

	void *dst = get_read_irp_data(irp, len);
	if (dst) {
		copy_iso_data(dst, r);
		vpdo->len_sent_partial = 0;
		irp->IoStatus.Information = len;
	}

	return dst ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

/*
 * USBD_START_ISO_TRANSFER_ASAP is appended because _URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
static NTSTATUS urb_isoch_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(r->PipeHandle);

	if (type != UsbdPipeTypeIsochronous) {
		TraceError(TRACE_READ, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 
					r->PipeHandle, r->TransferFlags | USBD_START_ISO_TRANSFER_ASAP, r->TransferBufferLength);

	if (err) {
		return err;
	}

	hdr->u.cmd_submit.start_frame = r->StartFrame;
	hdr->u.cmd_submit.number_of_packets = r->NumberOfPackets;

	irp->IoStatus.Information = sizeof(*hdr);

	if (get_read_payload_length(irp) >= get_iso_payload_len(r)) {
		copy_iso_data(hdr + 1, r);
		irp->IoStatus.Information += get_iso_payload_len(r);
	} else {
		urbr->vpdo->len_sent_partial = sizeof(*hdr);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS do_urb_control_transfer_partial(
	vpdo_dev_t *vpdo, IRP *irp, void *TransferBuffer, MDL *TransferBufferMDL, ULONG TransferBufferLength)
{
	void * dst = get_read_irp_data(irp, TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	const void *buf = get_buf(TransferBuffer, TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(dst, buf, TransferBufferLength);
		irp->IoStatus.Information = TransferBufferLength;
		vpdo->len_sent_partial = 0;
	}

	return ptr_to_status(buf);
}

static NTSTATUS urb_control_transfer_partial(vpdo_dev_t *vpdo, IRP *irp, URB *urb)
{
	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;
	return do_urb_control_transfer_partial(vpdo, irp, r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength);
}

static NTSTATUS urb_control_transfer_ex_partial(vpdo_dev_t *vpdo, IRP *irp, URB *urb)
{
	struct _URB_CONTROL_TRANSFER_EX	*r = &urb->UrbControlTransferEx;
	return do_urb_control_transfer_partial(vpdo, irp, r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength);
}

static NTSTATUS do_urb_control_transfer(
	IRP *irp, struct urb_req* urbr, bool dir_out,
	USBD_PIPE_HANDLE PipeHandle, ULONG TransferFlags, void *TransferBuffer, MDL *TransferBufferMDL, ULONG TransferBufferLength,
	const UCHAR *SetupPacket)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		TraceError(TRACE_READ, "Cannot get usbip header");
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (dir_out != IsTransferDirectionOut(TransferFlags)) {
		TraceError(TRACE_READ, "Transfer direction differs in TransferFlags(%#lx) and SetupPacket", TransferFlags);
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, PipeHandle, TransferFlags, TransferBufferLength);
	if (err) {
		return err;
	}

	RtlCopyMemory(hdr->u.cmd_submit.setup, SetupPacket, sizeof(hdr->u.cmd_submit.setup));
	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(USB_DEFAULT_PIPE_SETUP_PACKET), "assert");

	irp->IoStatus.Information = sizeof(*hdr);

	if (!(dir_out && TransferBufferLength)) {
		return STATUS_SUCCESS;
	}

	if (get_read_payload_length(irp) < TransferBufferLength) {
		urbr->vpdo->len_sent_partial = sizeof(*hdr);
		return STATUS_SUCCESS;
	}

	const void *buf = get_buf(TransferBuffer, TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(hdr + 1, buf, TransferBufferLength);
	}

	return ptr_to_status(buf);
}

static NTSTATUS urb_control_transfer(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;
	bool dir_out = is_transfer_dir_out(r);

	return do_urb_control_transfer(irp, urbr, dir_out,
		r->PipeHandle, r->TransferFlags, r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, r->SetupPacket);
}

static NTSTATUS urb_control_transfer_ex(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct _URB_CONTROL_TRANSFER_EX	*r = &urb->UrbControlTransferEx;
	bool dir_out = is_transfer_dir_out_ex(r);

	return do_urb_control_transfer(irp, urbr, dir_out,
		r->PipeHandle, r->TransferFlags, r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, r->SetupPacket);
}

/*
 * vhci_internal_ioctl.c handles such functions itself.
 */
static NTSTATUS urb_function_unexpected(IRP *irp, URB *urb, struct urb_req* urbr)
{
	UNREFERENCED_PARAMETER(irp);
	UNREFERENCED_PARAMETER(urbr);

	USHORT func = urb->UrbHeader.Function;
	TraceError(TRACE_READ, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	return STATUS_INTERNAL_ERROR;
}	

static NTSTATUS get_descriptor_from_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_IN, USB_RECIP_DEVICE);
}

static NTSTATUS set_descriptor_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_OUT, USB_RECIP_DEVICE);
}

static NTSTATUS get_descriptor_from_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_IN, USB_RECIP_INTERFACE);
}

static NTSTATUS set_descriptor_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_OUT, USB_RECIP_INTERFACE);
}

static NTSTATUS get_descriptor_from_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_IN, USB_RECIP_ENDPOINT);
}

static NTSTATUS set_descriptor_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, USB_DIR_OUT, USB_RECIP_ENDPOINT);
}

static NTSTATUS urb_control_feature_request(IRP *irp, URB *urb, struct urb_req* urbr, UCHAR bRequest, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_FEATURE_REQUEST *r = &urb->UrbControlFeatureRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | recipient;
	pkt->bRequest = bRequest;
	pkt->wValue.W = r->FeatureSelector; 
	pkt->wIndex.W = r->Index;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS set_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

static NTSTATUS set_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

static NTSTATUS set_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

static NTSTATUS set_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

static NTSTATUS clear_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

static NTSTATUS clear_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

static NTSTATUS clear_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

static NTSTATUS clear_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

static NTSTATUS get_configuration(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_GET_CONFIGURATION_REQUEST *r = &urb->UrbControlGetConfigurationRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, r->TransferBufferLength);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_GET_CONFIGURATION;
	pkt->wLength = (USHORT)r->TransferBufferLength; // must be 1

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS get_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_GET_INTERFACE_REQUEST *r = &urb->UrbControlGetInterfaceRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, r->TransferBufferLength);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
	pkt->bRequest = USB_REQUEST_GET_INTERFACE;
	pkt->wIndex.W = r->Interface;
	pkt->wLength = (USHORT)r->TransferBufferLength; // must be 1

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS get_status_from_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_DEVICE);
}

static NTSTATUS get_status_from_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_INTERFACE);
}

static NTSTATUS get_status_from_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_ENDPOINT);
}

static NTSTATUS get_status_from_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_OTHER);
}

typedef NTSTATUS (*urb_function_t)(IRP *irp, URB *urb, struct urb_req*);

static const urb_function_t urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_function_unexpected, // URB_FUNCTION_ABORT_PIPE, urb_pipe_request

	urb_function_unexpected, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	urb_function_unexpected, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	urb_function_unexpected, // URB_FUNCTION_GET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_GET_CURRENT_FRAME_NUMBER

	urb_control_transfer,
	urb_bulk_or_interrupt_transfer,
	urb_isoch_transfer,

	get_descriptor_from_device,
	set_descriptor_to_device,

	set_feature_to_device,
	set_feature_to_interface, 
	set_feature_to_endpoint,

	clear_feature_to_device,
	clear_feature_to_interface,
	clear_feature_to_endpoint,

	get_status_from_device,
	get_status_from_interface,
	get_status_from_endpoint,

	NULL, // URB_FUNCTION_RESERVED_0X0016          

	vendor_device,
	vendor_interface,
	vendor_endpoint,

	class_device,
	class_interface,
	class_endpoint,

	NULL, // URB_FUNCTION_RESERVE_0X001D

	sync_reset_pipe_and_clear_stall, // urb_pipe_request

	class_other,
	vendor_other,

	get_status_from_other,

	set_feature_to_other, 
	clear_feature_to_other,

	get_descriptor_from_endpoint,
	set_descriptor_to_endpoint,

	get_configuration, // URB_FUNCTION_GET_CONFIGURATION
	get_interface, // URB_FUNCTION_GET_INTERFACE

	get_descriptor_from_interface,
	set_descriptor_to_interface,

	urb_function_unexpected, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	NULL, // URB_FUNCTION_RESERVE_0X002B
	NULL, // URB_FUNCTION_RESERVE_0X002C
	NULL, // URB_FUNCTION_RESERVE_0X002D
	NULL, // URB_FUNCTION_RESERVE_0X002E
	NULL, // URB_FUNCTION_RESERVE_0X002F

	urb_function_unexpected, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	urb_function_unexpected, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_control_transfer_ex,

	NULL, // URB_FUNCTION_RESERVE_0X0033
	NULL, // URB_FUNCTION_RESERVE_0X0034                  

	urb_function_unexpected, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_function_unexpected, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	urb_bulk_or_interrupt_transfer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	urb_isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	NULL, // 0x0039
	NULL, // 0x003A        
	NULL, // 0x003B        
	NULL, // 0x003C        

	urb_function_unexpected // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

static NTSTATUS usb_submit_urb(IRP *irp, struct urb_req *urbr)
{
	URB *urb = URB_FROM_IRP(urbr->irp);
	if (!urb) {
		TraceError(TRACE_READ, "null urb");
		irp->IoStatus.Information = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	USHORT func = urb->UrbHeader.Function;

	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : NULL;
	if (pfunc) {
		return pfunc(irp, urb, urbr);
	}

	TraceError(TRACE_READ, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);

	irp->IoStatus.Information = 0;
	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS store_urbr_partial(IRP *read_irp, struct urb_req *urbr)
{
	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceVerbose(TRACE_READ, "Enter %s", urb_req_str(buf, sizeof(buf), urbr));
	}

	URB *urb = URB_FROM_IRP(urbr->irp);
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = urb_isoch_transfer_partial(urbr->vpdo, read_irp, urb);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = urb_bulk_or_interrupt_transfer_partial(urbr->vpdo, read_irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = urb_control_transfer_partial(urbr->vpdo, read_irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = urb_control_transfer_ex_partial(urbr->vpdo, read_irp, urb);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		status = urb_control_vendor_class_request_partial(urbr->vpdo, read_irp, urb);
		break;
	default:
		read_irp->IoStatus.Information = 0;
		TraceError(TRACE_READ, "%s: unexpected partial urbr", urb_function_str(urb->UrbHeader.Function));
	}

	TraceVerbose(TRACE_READ, "Leave %!STATUS!", status);
	return status;
}

static NTSTATUS store_cancelled_urbr(PIRP irp, struct urb_req *urbr)
{
	TraceInfo(TRACE_READ, "Enter");

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, urbr->seq_num_unlink);

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

NTSTATUS store_urbr(IRP *read_irp, struct urb_req *urbr)
{
	if (!urbr->irp) {
		return store_cancelled_urbr(read_irp, urbr);
	}

	NTSTATUS status = STATUS_INVALID_PARAMETER;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = usb_submit_urb(read_irp, urbr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = usb_reset_port(read_irp, urbr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_node_connection(read_irp, urbr);
		break;
	default:
		TraceWarning(TRACE_READ, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
		read_irp->IoStatus.Information = 0;
	}

	return status;
}

static void on_pending_irp_read_cancelled(DEVICE_OBJECT *devobj, IRP *irp_read)
{
	UNREFERENCED_PARAMETER(devobj);
	TraceInfo(TRACE_READ, "pending irp read cancelled %p", irp_read);

	IoReleaseCancelSpinLock(irp_read->CancelIrql);

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp_read);
	vpdo_dev_t *vpdo = irpstack->FileObject->FsContext;

	KIRQL irql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &irql);
	if (vpdo->pending_read_irp == irp_read) {
		vpdo->pending_read_irp = NULL;
	}
	KeReleaseSpinLock(&vpdo->lock_urbr, irql);

	irp_read->IoStatus.Information = 0;
	irp_done(irp_read, STATUS_CANCELLED);
}

static NTSTATUS process_read_irp(vpdo_dev_t *vpdo, IRP *read_irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	struct urb_req *urbr = NULL;
	KIRQL oldirql;

	TraceVerbose(TRACE_READ, "Enter");

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->pending_read_irp) {
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (vpdo->urbr_sent_partial) {
		urbr = vpdo->urbr_sent_partial;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = store_urbr_partial(read_irp, urbr);

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
		vpdo->len_sent_partial = 0;
	} else {
		urbr = find_pending_urbr(vpdo);
		if (!urbr) {
			vpdo->pending_read_irp = read_irp;

			KIRQL oldirql_cancel;
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(read_irp, on_pending_irp_read_cancelled);
			IoReleaseCancelSpinLock(oldirql_cancel);
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			IoMarkIrpPending(read_irp);

			return STATUS_PENDING;
		}

		vpdo->urbr_sent_partial = urbr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = store_urbr(read_irp, urbr);

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	}

	if (status != STATUS_SUCCESS) {
		RemoveEntryListInit(&urbr->list_all);
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		PIRP irp = urbr->irp;
		free_urbr(urbr);

		if (irp) {
			// urbr irp has cancel routine, if the IoSetCancelRoutine returns NULL that means IRP was cancelled
			IoAcquireCancelSpinLock(&oldirql);
			BOOLEAN valid = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql);
			if (valid) {
				irp->IoStatus.Information = 0;
				irp_done(irp, STATUS_INVALID_PARAMETER);
			}
		}
	} else {
		if (!vpdo->len_sent_partial) {
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
			vpdo->urbr_sent_partial = NULL;
		}
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
	}

	return status;
}

PAGEABLE NTSTATUS vhci_read(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();

	TraceVerbose(TRACE_READ, "Enter irql %!irql!", KeGetCurrentIrql());

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		TraceError(TRACE_READ, "read for non-vhci is not allowed");
		return  irp_done(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->common.DevicePnPState != Deleted) {
		IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
		vpdo_dev_t *vpdo = irpstack->FileObject->FsContext;
		status = vpdo && vpdo->plugged ? process_read_irp(vpdo, irp) : STATUS_INVALID_DEVICE_REQUEST;
	}

	if (status != STATUS_PENDING) {
		irp_done(irp, status);
	}

	TraceVerbose(TRACE_READ, "Leave %!STATUS!", status);
	return status;
}
