#include "vhci_read.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_read.tmh"

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

static NTSTATUS store_urb_reset_dev(IRP *irp, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, false, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_CLASS, BMREQUEST_TO_OTHER, USB_REQUEST_SET_FEATURE);
	setup->wValue.LowByte = 4; // Reset

	irp->IoStatus.Information = sizeof(*hdr);

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_dsc_req(PIRP irp, struct urb_req *urbr)
{
	struct usbip_header *hdr;
	PUSB_DESCRIPTOR_REQUEST dsc_req;
	PIO_STACK_LOCATION	irpstack;
	ULONG	outlen;

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	dsc_req = (USB_DESCRIPTOR_REQUEST*)urbr->irp->AssociatedIrp.SystemBuffer;

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength - sizeof(*dsc_req);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0, 0, outlen);
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);
	setup->wValue.W = dsc_req->SetupPacket.wValue;
	setup->wIndex.W = dsc_req->SetupPacket.wIndex;
	setup->wLength = dsc_req->SetupPacket.wLength;

	irp->IoStatus.Information = sizeof(*hdr);

	return STATUS_SUCCESS;
}

/* 
 * 1. We clear STALL/HALT feature on endpoint specified by pipe
 * 2. We abort/cancel all IRP for given pipe
 */
static NTSTATUS store_urb_reset_pipe(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_PIPE_REQUEST *urb_rp = &urb->UrbPipeRequest;

	if (get_endpoint_type(urb_rp->PipeHandle) == UsbdPipeTypeControl) {
		TraceWarning(TRACE_READ, "CLEAR not allowed to a control pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, false, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_ENDPOINT, USB_REQUEST_CLEAR_FEATURE);
	setup->wValue.W = USB_FEATURE_ENDPOINT_STALL;
	setup->wIndex.W = get_endpoint_address(urb_rp->PipeHandle);

	irp->IoStatus.Information = sizeof(*hdr);

	// cancel/abort all URBs for given pipe
	vhci_ioctl_abort_pipe(urbr->vpdo, urb_rp->PipeHandle);

	return STATUS_SUCCESS;
}

static PVOID get_buf(PVOID buf, PMDL bufMDL)
{
	if (!buf) {
		if (bufMDL) {
			buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
		}
		if (!buf) {
			TraceError(TRACE_READ, "No transfer buffer\n");
		}
	}

	return buf;
}

static NTSTATUS
store_urb_get_dev_desc(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);
	setup->wLength = (USHORT)urb_desc->TransferBufferLength;
	setup->wValue.HiByte = urb_desc->DescriptorType;
	setup->wValue.LowByte = urb_desc->Index;

	switch (urb_desc->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		//setup->wIndex.W = 0;
		break;
	case USB_INTERFACE_DESCRIPTOR_TYPE:
		setup->wIndex.W = urb_desc->Index;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		setup->wIndex.W = urb_desc->LanguageId;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_get_status(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_GET_STATUS_REQUEST	*urb_gsr = &urb->UrbControlGetStatusRequest;
	USHORT		code_func;
	char		recip;

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_gsr->TransferBufferLength);

	code_func = urb->UrbHeader.Function;
	TraceInfo(TRACE_READ, "urbr: %s, %!urb_function!\n", dbg_urbr(urbr), code_func);

	switch (code_func) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		TraceWarning(TRACE_IOCTL, "unhandled function: %!urb_function!, len %d\n",
			urb->UrbHeader.Function, urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, recip, USB_REQUEST_GET_STATUS);
	setup->wLength = (USHORT)urb_gsr->TransferBufferLength;
	setup->wIndex.W = urb_gsr->Index;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_get_intf_desc(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_desc = &urb->UrbControlDescriptorRequest;
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, USBIP_DIR_IN, 0,
				    USBD_SHORT_TRANSFER_OK, urb_desc->TransferBufferLength);
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_DESCRIPTOR);
	setup->wLength = (USHORT)urb_desc->TransferBufferLength;
	setup->wValue.HiByte = urb_desc->DescriptorType;
	setup->wValue.LowByte = urb_desc->Index;
	setup->wIndex.W = urb_desc->LanguageId;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_class_vendor_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vc = &urb->UrbControlVendorClassRequest;
	PVOID	dst;
	char    *buf;

	dst = get_read_irp_data(irp, urb_vc->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	buf = get_buf(urb_vc->TransferBuffer, urb_vc->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	RtlCopyMemory(dst, buf, urb_vc->TransferBufferLength);
	irp->IoStatus.Information = urb_vc->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_class_vendor(PIRP irp, PURB urb, struct urb_req *urbr)
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
store_urb_select_config(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_SELECT_CONFIGURATION *urb_sc = &urb->UrbSelectConfiguration;

	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 0, 0, 0, 0);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	setup->wValue.W = urb_sc->ConfigurationDescriptor->bConfigurationValue;

	irp->IoStatus.Information = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_select_interface(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_SELECT_INTERFACE	*urb_si = &urb->UrbSelectInterface;
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 0, 0, 0, 0);
	
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = init_setup_packet(hdr, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	setup->wValue.W = urb_si->Interface.AlternateSetting;
	setup->wIndex.W = urb_si->Interface.InterfaceNumber;

	irp->IoStatus.Information = sizeof(*hdr);
	return  STATUS_SUCCESS;
}

static NTSTATUS
store_urb_bulk_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
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
static NTSTATUS store_urb_bulk(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *urb_bi = &urb->UrbBulkOrInterruptTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(urb_bi->PipeHandle);

	if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
		TraceError(TRACE_READ, "Error, not a bulk/int pipe\n");
		return STATUS_INVALID_PARAMETER;
	}

	bool dir_in = is_endpoint_direction_in(urb_bi->PipeHandle);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, dir_in, urb_bi->PipeHandle,
				    urb_bi->TransferFlags, urb_bi->TransferBufferLength);

	irp->IoStatus.Information = sizeof(*hdr);

	if (!dir_in) {
		if (get_read_payload_length(irp) >= urb_bi->TransferBufferLength) {
			PVOID buf = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
			if (buf) {
				RtlCopyMemory(hdr + 1, buf, urb_bi->TransferBufferLength);
			} else {
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		} else {
			urbr->vpdo->len_sent_partial = sizeof(struct usbip_header);
		}
	}

	return STATUS_SUCCESS;
}

static NTSTATUS copy_iso_data(PVOID dst, struct _URB_ISOCH_TRANSFER *urb_iso)
{
	char *buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
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

static NTSTATUS
store_urb_iso_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
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

static NTSTATUS store_urb_iso(PIRP irp, PURB urb, struct urb_req *urbr)
{
	struct _URB_ISOCH_TRANSFER *urb_iso = &urb->UrbIsochronousTransfer;

	USBD_PIPE_TYPE type = get_endpoint_type(urb_iso->PipeHandle);
	if (type != UsbdPipeTypeIsochronous) {
		TraceError(TRACE_READ, "Error, not a iso pipe\n");
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

static NTSTATUS store_urb_control_transfer_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	struct _URB_CONTROL_TRANSFER *urb_ctltrans = &urb->UrbControlTransfer;

	PVOID dst = get_read_irp_data(irp, urb_ctltrans->TransferBufferLength);
	if (!dst) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	char *buf = get_buf(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL);
	if (!buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(dst, buf, urb_ctltrans->TransferBufferLength);
	irp->IoStatus.Information = urb_ctltrans->TransferBufferLength;
	vpdo->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_control_transfer(PIRP irp, PURB urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		TraceError(TRACE_READ, "Cannot get usbip header\n");
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

static NTSTATUS
store_urb_control_transfer_ex_partial(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
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

static NTSTATUS
store_urb_control_transfer_ex(PIRP irp, PURB urb, struct urb_req* urbr)
{
	struct usbip_header *hdr = get_usbip_hdr_from_read_irp(irp);
	if (!hdr) {
		TraceError(TRACE_READ, "Cannot get usbip header\n");
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

static NTSTATUS
store_urbr_submit(PIRP irp, struct urb_req *urbr)
{
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USHORT		code_func;
	NTSTATUS	status;

	TraceInfo(TRACE_READ, "store_urbr_submit: urbr: %s\n", dbg_urbr(urbr));

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	urb = irpstack->Parameters.Others.Argument1;
	if (urb == NULL) {
		TraceError(TRACE_READ, "store_urbr_submit: null urb\n");

		irp->IoStatus.Information = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	code_func = urb->UrbHeader.Function;
	TraceInfo(TRACE_READ, "urbr: %s, %!urb_function!\n", dbg_urbr(urbr), code_func);

	switch (code_func) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk(irp, urb, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso(irp, urb, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urb_get_dev_desc(irp, urb, urbr);
		break;
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = store_urb_get_status(irp, urb, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		status = store_urb_get_intf_desc(irp, urb, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urb_class_vendor(irp, urb, urbr);
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		status = store_urb_select_config(irp, urb, urbr);
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		status = store_urb_select_interface(irp, urb, urbr);
		break;
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		status = store_urb_reset_pipe(irp, urb, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urb_control_transfer(irp, urb, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urb_control_transfer_ex(irp, urb, urbr);
		break;
	default:
		irp->IoStatus.Information = 0;
		TraceError(TRACE_READ, "unhandled %!urb_function!\n", code_func);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static NTSTATUS
store_urbr_partial(PIRP irp, struct urb_req *urbr)
{
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USHORT		code_func;
	NTSTATUS	status;

	TraceInfo(TRACE_READ, "store_urbr_partial: Enter: urbr: %s\n", dbg_urbr(urbr));

	irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	urb = irpstack->Parameters.Others.Argument1;
	code_func = urb->UrbHeader.Function;

	switch (code_func) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urb_class_vendor_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urb_control_transfer_partial(urbr->vpdo, irp, urb);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urb_control_transfer_ex_partial(urbr->vpdo, irp, urb);
		break;
	default:
		irp->IoStatus.Information = 0;
		TraceError(TRACE_READ, "unexpected partial urbr: %!urb_function!\n", code_func);
		status = STATUS_INVALID_PARAMETER;
		break;
	}
	TraceInfo(TRACE_READ, "Leave %!STATUS!\n", status);

	return status;
}

static NTSTATUS
store_cancelled_urbr(PIRP irp, struct urb_req *urbr)
{
	struct usbip_header	*hdr;

	TraceInfo(TRACE_READ, "store_cancelled_urbr: Enter\n");

	hdr = get_usbip_hdr_from_read_irp(irp);
	if (hdr == NULL)
		return STATUS_INVALID_PARAMETER;

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, urbr->seq_num_unlink);

	irp->IoStatus.Information = sizeof(struct usbip_header);
	return STATUS_SUCCESS;
}

NTSTATUS store_urbr(IRP *irp, struct urb_req *urbr)
{
	TraceInfo(TRACE_READ, "store_urbr: urbr: %s\n", dbg_urbr(urbr));

	if (urbr->irp == NULL) {
		return store_cancelled_urbr(irp, urbr);
	}

	NTSTATUS status = STATUS_SUCCESS;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = store_urbr_submit(irp, urbr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = store_urb_reset_dev(irp, urbr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = store_urb_dsc_req(irp, urbr);
		break;
	default:
		TraceWarning(TRACE_READ, "unhandled ioctl: %!IOCTL!\n", ioctl_code);
		irp->IoStatus.Information = 0;
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static VOID
on_pending_irp_read_cancelled(PDEVICE_OBJECT devobj, PIRP irp_read)
{
	UNREFERENCED_PARAMETER(devobj);
	TraceInfo(TRACE_READ, "pending irp read cancelled %p\n", irp_read);

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

	irp_read->IoStatus.Status = STATUS_CANCELLED;
	irp_read->IoStatus.Information = 0;
	IoCompleteRequest(irp_read, IO_NO_INCREMENT);
}

static NTSTATUS process_read_irp(vpdo_dev_t *vpdo, IRP *read_irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	struct urb_req *urbr = NULL;
	KIRQL oldirql;

	TraceInfo(TRACE_READ, "Enter\n");

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
	}
	else {
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
				irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
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

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceInfo(TRACE_READ, "Enter: len:%u, irp:%p\n", irpstack->Parameters.Read.Length, irp);

	if (!IS_DEVOBJ_VHCI(devobj)) {
		TraceError(TRACE_READ, "read for non-vhci is not allowed\n");

		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	vhci_dev_t *vhci = DEVOBJ_TO_VHCI(devobj);

	// Check to see whether the bus is removed
	if (vhci->common.DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	vpdo_dev_t *vpdo = irpstack->FileObject->FsContext;
	status = vpdo && vpdo->plugged ? process_read_irp(vpdo, irp) : STATUS_INVALID_DEVICE_REQUEST;

END:
	TraceInfo(TRACE_READ, "Leave: irp %p, %!STATUS!\n", irp, status);

	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	return status;
}
