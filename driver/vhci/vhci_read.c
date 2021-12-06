#include "vhci_read.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_read.tmh"

#include "vhci_irp.h"
#include "vhci_proto.h"
#include "vhci_internal_ioctl.h"
#include "usbd_helper.h"

static PVOID get_read_irp_data(IRP *irp, ULONG length)
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

static NTSTATUS usb_reset_port(IRP *irp, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_OUT, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_OTHER, USB_REQUEST_SET_FEATURE);
	setup->wValue.LowByte = 4; // Reset

	irp->IoStatus.Information = sizeof(*hdr);

	return STATUS_SUCCESS;
}

/*
 * USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);
 */
static NTSTATUS get_descriptor_from_node_connection(IRP *irp, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	USB_DESCRIPTOR_REQUEST *r = urbr->irp->AssociatedIrp.SystemBuffer;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength - sizeof(*r);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0, 0, outlen);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);
	static_assert(sizeof(*setup) == sizeof(r->SetupPacket), "assert");
	RtlCopyMemory(setup, &r->SetupPacket, sizeof(*setup));

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

/* 
 * 1. We clear STALL/HALT feature on endpoint specified by pipe
 * 2. We abort/cancel all IRP for given pipe
 */
static NTSTATUS sync_reset_pipe_and_clear_stall(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_PIPE_REQUEST *urb_rp = &urb->UrbPipeRequest;

	if (get_endpoint_type(urb_rp->PipeHandle) == UsbdPipeTypeControl) {
		TraceWarning(TRACE_READ, "CLEAR not allowed to a control pipe");
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_OUT, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_ENDPOINT, USB_REQUEST_CLEAR_FEATURE);
	setup->wValue.W = USB_FEATURE_ENDPOINT_STALL;
	setup->wIndex.W = get_endpoint_address(urb_rp->PipeHandle);

	irp->IoStatus.Information = sizeof(*hdr);

	// cancel/abort all URBs for given pipe
	vhci_ioctl_abort_pipe(urbr->vpdo, urb_rp->PipeHandle);

	return STATUS_SUCCESS;
}

static void *get_buf(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}

	if (bufMDL) {
		buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
	}

	if (!buf) {
		TraceError(TRACE_READ, "No transfer buffer");
	}

	return buf;
}

static NTSTATUS urb_control_descriptor_request(IRP *irp, URB *urb, struct urb_req *urbr, bool dir_in, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, 0, USBD_SHORT_TRANSFER_OK, r->TransferBufferLength);
	
	UCHAR dir = dir_in ? BMREQUEST_DEVICE_TO_HOST : BMREQUEST_HOST_TO_DEVICE;
	UCHAR request = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, dir, BMREQUEST_STANDARD, recipient, request);

	setup->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r->DescriptorType, r->Index); 
	setup->wIndex.W = r->LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
	setup->wLength = (USHORT)r->TransferBufferLength;

	irp->IoStatus.Information = sizeof(*hdr);

	if (dir_in) {
		return STATUS_SUCCESS;
	}
	
	if (get_read_payload_length(irp) < r->TransferBufferLength) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(hdr + 1, buf, r->TransferBufferLength);
		irp->IoStatus.Information += r->TransferBufferLength;
	}

	return buf ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static int get_recipient(int usb_function)
{
	switch (usb_function) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
		return BMREQUEST_TO_DEVICE;
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
		return BMREQUEST_TO_INTERFACE;
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
		return BMREQUEST_TO_ENDPOINT;
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		return BMREQUEST_TO_OTHER;
	}

	NT_FRE_ASSERT(!"Unexpected usb function");
	return -1;
}

static NTSTATUS urb_control_get_status_request(IRP *irp, URB *urb, struct urb_req *urbr)
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

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, r->TransferBufferLength);

	int recip = get_recipient(urb->UrbHeader.Function);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, 
								(UCHAR)recip, USB_REQUEST_GET_STATUS);

	setup->wIndex.W = r->Index;
	setup->wLength = (USHORT)r->TransferBufferLength;
	
	if (r->TransferBufferLength != 2) {
		TraceWarning(TRACE_READ, "TransferBufferLength %lu != 2", r->TransferBufferLength);
	}

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_vendor_class_request_partial(vpdo_dev_t *vpdo, IRP *irp, URB *urb)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *urb_vc = &urb->UrbControlVendorClassRequest;

	void *dst = get_read_irp_data(irp, urb_vc->TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	void *buf = get_buf(urb_vc->TransferBuffer, urb_vc->TransferBufferMDL);
	if (!buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(dst, buf, urb_vc->TransferBufferLength);
	irp->IoStatus.Information = urb_vc->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
urb_control_vendor_class_request(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vc = &urb->UrbControlVendorClassRequest;
	char	type, recip;
	bool dir_in = IsTransferDirectionIn(urb_vc->TransferFlags);

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	switch (urb_vc->Hdr.Function) {
	case URB_FUNCTION_CLASS_DEVICE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_CLASS_INTERFACE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_CLASS_ENDPOINT:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_CLASS_OTHER:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_OTHER;
		break;
	case URB_FUNCTION_VENDOR_DEVICE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_VENDOR_INTERFACE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_VENDOR_ENDPOINT:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_VENDOR_OTHER:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, 0,
				    urb_vc->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_vc->TransferBufferLength);
	
	UCHAR dir = dir_in ? BMREQUEST_DEVICE_TO_HOST : BMREQUEST_HOST_TO_DEVICE;

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, dir, type, recip, urb_vc->Request);
	setup->wLength = (USHORT)urb_vc->TransferBufferLength;
	setup->wValue.W = urb_vc->Value;
	setup->wIndex.W = urb_vc->Index;

	irp->IoStatus.Information = sizeof(*hdr);

	if (!dir_in) {
		if (get_read_payload_length(irp) >= urb_vc->TransferBufferLength) {
			RtlCopyMemory(hdr + 1, urb_vc->TransferBuffer, urb_vc->TransferBufferLength);
			irp->IoStatus.Information += urb_vc->TransferBufferLength;
		} else {
			urbr->vpdo->len_sent_partial = sizeof(*hdr);
		}
	}

	return  STATUS_SUCCESS;
}

static NTSTATUS
urb_select_configuration(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_SELECT_CONFIGURATION *urb_sc = &urb->UrbSelectConfiguration;

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_OUT, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	setup->wValue.W = urb_sc->ConfigurationDescriptor->bConfigurationValue;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS
urb_select_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_SELECT_INTERFACE	*urb_si = &urb->UrbSelectInterface;
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_OUT, 0, 0, 0);
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	setup->wValue.W = urb_si->Interface.AlternateSetting;
	setup->wIndex.W = urb_si->Interface.InterfaceNumber;

	irp->IoStatus.Information = sizeof(*hdr);
	return  STATUS_SUCCESS;
}

static NTSTATUS urb_bulk_or_interrupt_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*urb_bi = &urb->UrbBulkOrInterruptTransfer;
	PVOID	dst, src;

	dst = get_read_irp_data(irp, urb_bi->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	src = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
	if (src == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, src, urb_bi->TransferBufferLength);
	irp->IoStatus.Information = urb_bi->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

/* 
 * Sometimes, direction in TransferFlags of _URB_BULK_OR_INTERRUPT_TRANSFER is not consistent 
 * with PipeHandle. Use a direction flag in pipe handle.
 */
static NTSTATUS urb_bulk_or_interrupt_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *urb_bi = &urb->UrbBulkOrInterruptTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(urb_bi->PipeHandle);

	if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
		TraceError(TRACE_READ, "Error, not a bulk/int pipe");
		return STATUS_INVALID_PARAMETER;
	}

	bool dir_in = is_endpoint_direction_in(urb_bi->PipeHandle);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, urb_bi->PipeHandle,
				    urb_bi->TransferFlags, urb_bi->TransferBufferLength);

	irp->IoStatus.Information = sizeof(*hdr);

	if (!dir_in) {
		if (get_read_payload_length(irp) >= urb_bi->TransferBufferLength) {

			void *buf_a = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? 
					NULL : urb_bi->TransferBuffer;

			void *buf = get_buf(buf_a, urb_bi->TransferBufferMDL);
			if (buf) {
				RtlCopyMemory(hdr + 1, buf, urb_bi->TransferBufferLength);
			} else {
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		} else {
			urbr->vpdo->len_sent_partial = sizeof(*hdr);
		}
	}

	return STATUS_SUCCESS;
}

static NTSTATUS copy_iso_data(PVOID dst, struct _URB_ISOCH_TRANSFER *urb_iso)
{
	void *buf_a = urb_iso->Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? NULL : urb_iso->TransferBuffer;

	void *buf = get_buf(buf_a, urb_iso->TransferBufferMDL);
	if (!buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	struct usbip_iso_packet_descriptor *iso_desc = NULL;

	if (is_endpoint_direction_in(urb_iso->PipeHandle)) {
		iso_desc = (struct usbip_iso_packet_descriptor *)dst;
	} else {
		RtlCopyMemory(dst, buf, urb_iso->TransferBufferLength);
		iso_desc = (struct usbip_iso_packet_descriptor *)((char *)dst + urb_iso->TransferBufferLength);
	}

	ULONG offset = 0;

	for (ULONG i = 0; i < urb_iso->NumberOfPackets; ++i) {
		if (urb_iso->IsoPacket[i].Offset < offset) {
			TraceWarning(TRACE_READ, "strange iso packet offset:%d %d", offset, urb_iso->IsoPacket[i].Offset);
			return STATUS_INVALID_PARAMETER;
		}
		iso_desc->offset = urb_iso->IsoPacket[i].Offset;
		if (i > 0) {
			(iso_desc - 1)->length = urb_iso->IsoPacket[i].Offset - offset;
		}
		offset = urb_iso->IsoPacket[i].Offset;
		iso_desc->actual_length = 0;
		iso_desc->status = 0;
		++iso_desc;
	}

	(iso_desc - 1)->length = urb_iso->TransferBufferLength - offset;

	return STATUS_SUCCESS;
}

static ULONG
get_iso_payload_len(struct _URB_ISOCH_TRANSFER *urb_iso)
{
	ULONG len_iso = urb_iso->NumberOfPackets * sizeof(struct usbip_iso_packet_descriptor);
	
	if (is_endpoint_direction_out(urb_iso->PipeHandle)) {
		len_iso += urb_iso->TransferBufferLength;
	}

	return len_iso;
}

static NTSTATUS urb_isoch_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_ISOCH_TRANSFER *urb_iso = &urb->UrbIsochronousTransfer;
	ULONG	len_iso;
	PVOID	dst;

	len_iso = get_iso_payload_len(urb_iso);

	dst = get_read_irp_data(irp, len_iso);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	copy_iso_data(dst, urb_iso);
	vpdo->len_sent_partial = 0;
	irp->IoStatus.Information = len_iso;

	return STATUS_SUCCESS;
}

static NTSTATUS urb_isoch_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_ISOCH_TRANSFER *urb_iso = &urb->UrbIsochronousTransfer;

	USBD_PIPE_TYPE type = get_endpoint_type(urb_iso->PipeHandle);
	if (type != UsbdPipeTypeIsochronous) {
		TraceError(TRACE_READ, "Error, not a iso pipe");
		return STATUS_INVALID_PARAMETER;
	}

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	bool dir_in = is_endpoint_direction_in(urb_iso->PipeHandle);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid,
				    dir_in, urb_iso->PipeHandle, urb_iso->TransferFlags,
				    urb_iso->TransferBufferLength);

	hdr->u.cmd_submit.start_frame = urb_iso->StartFrame;
	hdr->u.cmd_submit.number_of_packets = urb_iso->NumberOfPackets;

	irp->IoStatus.Information = sizeof(*hdr);

	if (get_read_payload_length(irp) >= get_iso_payload_len(urb_iso)) {
		copy_iso_data(hdr + 1, urb_iso);
		irp->IoStatus.Information += get_iso_payload_len(urb_iso);
	} else {
		urbr->vpdo->len_sent_partial = sizeof(*hdr);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_CONTROL_TRANSFER *urb_ctltrans = &urb->UrbControlTransfer;

	PVOID dst = get_read_irp_data(irp, urb_ctltrans->TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	void *buf = get_buf(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL);
	if (!buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(dst, buf, urb_ctltrans->TransferBufferLength);
	irp->IoStatus.Information = urb_ctltrans->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_transfer(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		TraceError(TRACE_READ, "Cannot get usbip header");
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_TRANSFER *urb_ctltrans = &urb->UrbControlTransfer;
	bool dir_in = IsTransferDirectionIn(urb_ctltrans->TransferFlags);

	ULONG TransferFlags = urb_ctltrans->TransferFlags | USBD_SHORT_TRANSFER_OK;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, urb_ctltrans->PipeHandle,
		TransferFlags, urb_ctltrans->TransferBufferLength);

	RtlCopyMemory(hdr->u.cmd_submit.setup, urb_ctltrans->SetupPacket, sizeof(urb_ctltrans->SetupPacket));
	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(urb_ctltrans->SetupPacket), "assert");

	irp->IoStatus.Information = sizeof(*hdr);

	if (!dir_in && urb_ctltrans->TransferBufferLength > 0) {
		if (get_read_payload_length(irp) >= urb_ctltrans->TransferBufferLength) {
			PVOID buf = get_buf(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL);
			if (buf) {
				RtlCopyMemory(hdr + 1, buf, urb_ctltrans->TransferBufferLength);
			} else {
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		} else {
			urbr->vpdo->len_sent_partial = sizeof(struct usbip_header);
		}
	}

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_transfer_ex_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_CONTROL_TRANSFER_EX	*urb_control_ex = &urb->UrbControlTransferEx;
	PVOID	dst;
	char	*buf;

	dst = get_read_irp_data(irp, urb_control_ex->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	buf = get_buf(urb_control_ex->TransferBuffer, urb_control_ex->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, buf, urb_control_ex->TransferBufferLength);
	irp->IoStatus.Information = urb_control_ex->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_transfer_ex(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		TraceError(TRACE_READ, "Cannot get usbip header");
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_TRANSFER_EX	*urb_control_ex = &urb->UrbControlTransferEx;
	bool dir_in = is_endpoint_direction_in(urb_control_ex->PipeHandle);
	ULONG TransferFlags = urb_control_ex->TransferFlags | USBD_SHORT_TRANSFER_OK;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, urb_control_ex->PipeHandle,
		TransferFlags, urb_control_ex->TransferBufferLength);

	RtlCopyMemory(hdr->u.cmd_submit.setup, urb_control_ex->SetupPacket, sizeof(urb_control_ex->SetupPacket));
	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(urb_control_ex->SetupPacket), "assert");

	irp->IoStatus.Information = sizeof(*hdr);

	if (!dir_in) {
		if (get_read_payload_length(irp) >= urb_control_ex->TransferBufferLength) {
			PVOID buf = get_buf(urb_control_ex->TransferBuffer, urb_control_ex->TransferBufferMDL);
			if (buf) {
				RtlCopyMemory(hdr + 1, buf, urb_control_ex->TransferBufferLength);
			} else {
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		} else {
			urbr->vpdo->len_sent_partial = sizeof(*hdr);
		}
	}

	return STATUS_SUCCESS;
}

/*
 * vhci_internal_ioctl.c handles such functions itself, so this function must never be called.
 */
static NTSTATUS urb_function_unexpected(IRP *irp, URB *urb, struct urb_req* urbr)
{
	UNREFERENCED_PARAMETER(irp);
	UNREFERENCED_PARAMETER(urb);
	UNREFERENCED_PARAMETER(urbr);

	TraceError(TRACE_READ, "Internal logic error");
	return STATUS_INTERNAL_ERROR;
}	

static NTSTATUS get_descriptor_from_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_TO_DEVICE);
}

static NTSTATUS set_descriptor_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_TO_DEVICE);
}

static NTSTATUS get_descriptor_from_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_TO_INTERFACE);
}

static NTSTATUS set_descriptor_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_TO_INTERFACE);
}

static NTSTATUS get_descriptor_from_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_TO_ENDPOINT);
}

static NTSTATUS set_descriptor_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_TO_ENDPOINT);
}

static NTSTATUS urb_control_feature_request(IRP *irp, URB *urb, struct urb_req* urbr, UCHAR bRequest, UCHAR recipient)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_FEATURE_REQUEST *r = &urb->UrbControlFeatureRequest;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_OUT, 0, USBD_SHORT_TRANSFER_OK, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, recipient, bRequest);
	setup->wValue.W = r->FeatureSelector; 
	setup->wIndex.W = r->Index;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS set_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, BMREQUEST_TO_DEVICE);
}

static NTSTATUS set_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, BMREQUEST_TO_INTERFACE);
}

static NTSTATUS set_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, BMREQUEST_TO_ENDPOINT);
}

static NTSTATUS set_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, BMREQUEST_TO_OTHER);
}

static NTSTATUS clear_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, BMREQUEST_TO_DEVICE);
}

static NTSTATUS clear_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, BMREQUEST_TO_INTERFACE);
}

static NTSTATUS clear_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, BMREQUEST_TO_ENDPOINT);
}

static NTSTATUS clear_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, BMREQUEST_TO_OTHER);
}

static NTSTATUS get_configuration(IRP *irp, URB *urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_GET_CONFIGURATION_REQUEST *r = &urb->UrbControlGetConfigurationRequest;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0, 0, r->TransferBufferLength);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, 
									BMREQUEST_TO_DEVICE, USB_REQUEST_GET_CONFIGURATION);

	setup->wLength = (USHORT)r->TransferBufferLength; // must be 1

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

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0, 0, r->TransferBufferLength);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, 
									BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_CONFIGURATION);

	setup->wIndex.W = r->Interface;
	setup->wLength = (USHORT)r->TransferBufferLength; // must be 1

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
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

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_DEVICE
	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_INTERFACE
	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_ENDPOINT

	NULL, // URB_FUNCTION_RESERVED_0X0016          

	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_DEVICE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_DEVICE 
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_ENDPOINT

	NULL, // URB_FUNCTION_RESERVE_0X001D

	sync_reset_pipe_and_clear_stall, // urb_pipe_request

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_OTHER
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_OTHER

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_OTHER

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

	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(TRACE_READ, "%s: %s", urb_function_str(func), urb_req_str(buf, sizeof(buf), urbr));
	}

	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : NULL;
	if (pfunc) {
		return pfunc(irp, urb, urbr);
	}

	TraceError(TRACE_READ, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);

	irp->IoStatus.Information = 0;
	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS store_urbr_partial(IRP *irp, struct urb_req *urbr)
{
	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(TRACE_READ, "Enter %s", urb_req_str(buf, sizeof(buf), urbr));
	}

	URB *urb = URB_FROM_IRP(urbr->irp);
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = urb_isoch_transfer_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = urb_bulk_or_interrupt_transfer_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = urb_control_transfer_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = urb_control_transfer_ex_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		status = urb_control_vendor_class_request_partial(urbr->vpdo, irp, urb);
		break;
	default:
		irp->IoStatus.Information = 0;
		TraceError(TRACE_READ, "%s: unexpected partial urbr", urb_function_str(urb->UrbHeader.Function));
	}

	TraceInfo(TRACE_READ, "Leave %!STATUS!", status);
	return status;
}

static NTSTATUS
store_cancelled_urbr(PIRP irp, struct urb_req *urbr)
{
	struct usbip_header	*hdr;

	TraceInfo(TRACE_READ, "Enter");

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL)
		return STATUS_INVALID_PARAMETER;

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, urbr->seq_num_unlink);

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

NTSTATUS store_urbr(IRP *irp, struct urb_req *urbr)
{
	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(TRACE_READ, "%s", urb_req_str(buf, sizeof(buf), urbr));
	}

	if (!urbr->irp) {
		return store_cancelled_urbr(irp, urbr);
	}

	NTSTATUS status = STATUS_INVALID_PARAMETER;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = usb_submit_urb(irp, urbr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = usb_reset_port(irp, urbr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_node_connection(irp, urbr);
		break;
	default:
		TraceWarning(TRACE_READ, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
		irp->IoStatus.Information = 0;
	}

	return status;
}

static VOID
on_pending_irp_read_cancelled(PDEVICE_OBJECT devobj, PIRP irp_read)
{
	UNREFERENCED_PARAMETER(devobj);
	TraceInfo(TRACE_READ, "pending irp read cancelled %p", irp_read);

	PIO_STACK_LOCATION	irpstack;
	pvpdo_dev_t	vpdo;

	IoReleaseCancelSpinLock(irp_read->CancelIrql);

	irpstack = IoGetCurrentIrpStackLocation(irp_read);
	vpdo = irpstack->FileObject->FsContext;

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

	TraceInfo(TRACE_READ, "Enter");

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

	TraceInfo(TRACE_READ, "Leave %!STATUS!", status);
	return status;
}
