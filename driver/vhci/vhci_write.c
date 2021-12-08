#include "vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_write.tmh"

#include "usbip_proto.h"
#include "usbreq.h"
#include "usbd_helper.h"
#include "vhci_vpdo.h"
#include "vhci_vpdo_dsc.h"
#include "usbreq.h"
#include "usbd_helper.h"
#include "vhci_irp.h"

static bool update_iso_packets(USBD_ISO_PACKET_DESCRIPTOR *dst, ULONG cnt, const struct usbip_iso_packet_descriptor *src)
{
	for (ULONG i = 0; i < cnt; ++i, ++dst, ++src) {

		if (dst->Offset >= src->offset) {
			dst->Length = src->actual_length;
			dst->Status = to_windows_status(src->status);
		} else {
			TraceWarning(TRACE_WRITE, "#%lu: Offset(%lu) >= offset(%u)", i, dst->Offset, src->offset);
			return false;
		}
	}

	return true;
}

static void *get_buf(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}
	
	if (bufMDL) {
		buf = MmGetSystemAddressForMdlSafe(bufMDL, NormalPagePriority | MdlMappingNoExecute);
	}

	if (!buf) {
		TraceWarning(TRACE_WRITE, "No transfer buffer");
	}

	return buf;
}

static void copy_iso_data(char *dest, ULONG dest_len, const char *src, ULONG src_len, const struct _URB_ISOCH_TRANSFER *urb)
{
	ULONG src_offset = 0;

	for (ULONG i = 0; i < urb->NumberOfPackets; ++i) {
	
		const USBD_ISO_PACKET_DESCRIPTOR *p = urb->IsoPacket + i;
		if (!p->Length) {
			continue;
		}

		if (p->Offset + p->Length > dest_len) {
			TraceWarning(TRACE_WRITE, "#%lu: Offset(%lu) + Length(%lu) > dest_len(%lu)", i, p->Offset, p->Length, dest_len);
			break;
		}

		if (src_offset + p->Length > src_len) {
			TraceWarning(TRACE_WRITE, "#%lu:src_offset(%lu) + Length(%lu) > src_len(%lu)", i, src_offset, p->Length, src_len);
			break;
		}

		RtlCopyMemory(dest + p->Offset, src + src_offset, p->Length);
		src_offset += p->Length;
	}

	if (src_offset != src_len) {
		TraceWarning(TRACE_WRITE, "src_offset(%lu) != src_len(%lu)", src_offset, src_len);
	}
}

static NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(hdr);
	return vpdo_select_config(vpdo, &urb->UrbSelectConfiguration);
}

static NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(hdr);
	return vpdo_select_interface(vpdo, &urb->UrbSelectInterface);
}

static void *copy_to_transfer_buffer(void *buf_dst, MDL *bufMDL, ULONG dst_len, const void *src, int src_len)
{
	if (!(src_len >= 0 && dst_len >= (ULONG)src_len)) {
		TraceError(TRACE_WRITE, "Buffer length check: dst_len %lu, src_len %d", dst_len, src_len);
		return NULL;
	}

	void *buf = get_buf(buf_dst, bufMDL);

	if (buf) {
		RtlCopyMemory(buf, src, src_len);
	} else {
		TraceError(TRACE_WRITE, "Null destination buffers");
	}

	return buf;
}

static NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;
	int actual_length = hdr->u.ret_submit.actual_length;
	
	USB_COMMON_DESCRIPTOR *dsc = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, 
						hdr + 1, actual_length);

	if (!dsc) {
		return STATUS_INVALID_PARAMETER;
	}

	r->TransferBufferLength = actual_length;

	if (actual_length >= dsc->bLength) {
		try_to_cache_descriptor(vpdo, r, dsc);
	} else {
		TraceWarning(TRACE_WRITE, "skip to cache partial descriptor: bLength(%d) > TransferBufferLength(%d)", 
						dsc->bLength, actual_length);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_get_status_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_STATUS_REQUEST *r = &urb->UrbControlGetStatusRequest;
	int actual_length = hdr->u.ret_submit.actual_length;

	void *buf = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, hdr + 1, actual_length);
	if (buf) {
		r->TransferBufferLength = actual_length;
	}
	
	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS urb_control_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;

	if (!r->TransferBufferLength) {
		return STATUS_SUCCESS;
	}
	
	int actual_length = hdr->u.ret_submit.actual_length;

	void *buf = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, hdr + 1, actual_length);
	if (buf) {
		r->TransferBufferLength = actual_length;
	}
	
	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS urb_control_transfer_ex(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header* hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER_EX	*r = &urb->UrbControlTransferEx;

	if (!r->TransferBufferLength) {
		return STATUS_SUCCESS;
	}

	int actual_length = hdr->u.ret_submit.actual_length;

	void *buf = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, hdr + 1, actual_length);
	if (buf) {
		r->TransferBufferLength = actual_length;
	}
	
	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS urb_control_vendor_class_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;
	int actual_length = hdr->u.ret_submit.actual_length;

	if (IsTransferDirectionOut(r->TransferFlags)) {
		r->TransferBufferLength = actual_length;
		return STATUS_SUCCESS;
	}

	void *buf = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, hdr + 1, actual_length);
	if (buf) {
		r->TransferBufferLength = actual_length;
	}

	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS urb_bulk_or_interrupt_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;

	if (is_endpoint_direction_out(r->PipeHandle)) {
		return STATUS_SUCCESS;
	}

	int actual_length = hdr->u.ret_submit.actual_length;

	void *buf = copy_to_transfer_buffer(r->TransferBuffer, r->TransferBufferMDL, r->TransferBufferLength, hdr + 1, actual_length);
	if (buf) {
		r->TransferBufferLength = actual_length;
	}

	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * Layout: usbip_header, data(IN only), usbip_iso_packet_descriptor[]
 */
static NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	int cnt = hdr->u.ret_submit.number_of_packets;

	if (!(cnt >= 0 && (ULONG)cnt == r->NumberOfPackets)) {
		TraceError(TRACE_WRITE, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r->NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}
	
	const char *usbip_data = (char*)(hdr + 1);
	int actual_length = hdr->u.ret_submit.actual_length;

	{
		int data_len = is_endpoint_direction_in(r->PipeHandle) ? actual_length : 0;
		const void *d = usbip_data + data_len; // usbip_iso_packet_descriptor*
		if (!update_iso_packets(r->IsoPacket, r->NumberOfPackets, d)) {
			return STATUS_INVALID_PARAMETER;
		}
	}

	r->ErrorCount = hdr->u.ret_submit.error_count;

	if (r->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r->StartFrame = hdr->u.ret_submit.start_frame;
	}

	void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		copy_iso_data(buf, r->TransferBufferLength, usbip_data, actual_length, r);
		r->TransferBufferLength = actual_length;
	}

	return buf ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS sync_reset_pipe_and_clear_stall(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(urb);
	UNREFERENCED_PARAMETER(hdr);
	return STATUS_SUCCESS;
}

typedef NTSTATUS (*urb_function_t)(vpdo_dev_t*, URB*, const struct usbip_header*);

static const urb_function_t urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	NULL, // URB_FUNCTION_ABORT_PIPE, urb_pipe_request

	NULL, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	NULL, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	NULL, // URB_FUNCTION_GET_FRAME_LENGTH
	NULL, // URB_FUNCTION_SET_FRAME_LENGTH
	NULL, // URB_FUNCTION_GET_CURRENT_FRAME_NUMBER

	urb_control_transfer,
	urb_bulk_or_interrupt_transfer,
	urb_isoch_transfer,

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	NULL, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE, urb_control_descriptor_request

	NULL, // URB_FUNCTION_SET_FEATURE_TO_DEVICE, urb_control_feature_request
	NULL, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE, urb_control_feature_request
	NULL, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, urb_control_feature_request

	NULL, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE, urb_control_feature_request
	NULL, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE, urb_control_feature_request
	NULL, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT, urb_control_feature_request

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

	NULL, // URB_FUNCTION_SET_FEATURE_TO_OTHER, urb_control_feature_request
	NULL, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER, urb_control_feature_request

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	NULL, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT, urb_control_descriptor_request

	NULL, // URB_FUNCTION_GET_CONFIGURATION
	NULL, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	NULL, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE, urb_control_descriptor_request

	NULL, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	NULL, // URB_FUNCTION_RESERVE_0X002B
	NULL, // URB_FUNCTION_RESERVE_0X002C
	NULL, // URB_FUNCTION_RESERVE_0X002D
	NULL, // URB_FUNCTION_RESERVE_0X002E
	NULL, // URB_FUNCTION_RESERVE_0X002F

	NULL, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	NULL, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_control_transfer_ex,
/*
	NULL, // URB_FUNCTION_RESERVE_0X0033
	NULL, // URB_FUNCTION_RESERVE_0X0034                  

	NULL, // URB_FUNCTION_OPEN_STATIC_STREAMS
	NULL, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	NULL, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	NULL, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	NULL, // 0x0039
	NULL, // 0x003A        
	NULL, // 0x003B        
	NULL, // 0x003C        

	NULL // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
*/
};

static NTSTATUS store_urb_data(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	USHORT func = urb->UrbHeader.Function;
	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : NULL;

	if (!pfunc) {
		TraceVerbose(TRACE_WRITE, "Not handled %s(%#04x)", urb_function_str(func), func);
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS st = pfunc(vpdo, urb, hdr);

	if (st == STATUS_SUCCESS) {
		urb->UrbHeader.Status = to_windows_status(hdr->u.ret_submit.status);
	}

	return st;
}

static NTSTATUS internal_usb_submit_urb(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	if (!urb) {
		return STATUS_INVALID_PARAMETER;
	}

	const struct usbip_header_ret_submit *ret_submit = &hdr->u.ret_submit;

	int linux_status = ret_submit->status;
	if (!linux_status) {
		return store_urb_data(vpdo, urb, hdr);
	}

	USBD_STATUS status = to_windows_status(linux_status);
	urb->UrbHeader.Status = status;

	if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
		urb->UrbBulkOrInterruptTransfer.TransferBufferLength = ret_submit->actual_length;
	}

	TraceError(TRACE_WRITE, "%s: errno %d -> %s(%#08lX)", urb_function_str(urb->UrbHeader.Function), 
				linux_status, dbg_usbd_status(status), (ULONG)status);

	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS get_descriptor_from_node_connection(struct urb_req *urbr, const struct usbip_header *hdr)
{
	int linux_status = hdr->u.ret_submit.status;
	if (linux_status) {
		USBD_STATUS st = to_windows_status(linux_status);
		TraceError(TRACE_WRITE, "errno %d -> %s(%#08lX)", linux_status, dbg_usbd_status(st), (ULONG)st);
		return STATUS_UNSUCCESSFUL;
	}

	IRP *irp = urbr->irp;
        IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	USB_DESCRIPTOR_REQUEST *req = NULL;
	int actual_length = hdr->u.ret_submit.actual_length;

	ULONG sz = actual_length + sizeof(*req);
        irp->IoStatus.Information = sz;

	ULONG OutputBufferLength = irpstack->Parameters.DeviceIoControl.OutputBufferLength;
        if (sz > OutputBufferLength) {
		TraceError(TRACE_WRITE, "%lu > OutputBufferLength %lu", sz, OutputBufferLength);
		return irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
	}

	req = irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(req->Data, hdr + 1, actual_length);
		
	char buf[USB_SETUP_PKT_STR_BUFBZ];
	TraceVerbose(TRACE_WRITE, "ConnectionIndex %lu, %s, Data[%!BIN!]", 
				req->ConnectionIndex, 
				usb_setup_pkt_str(buf, sizeof(buf), &req->SetupPacket),
				WppBinary(req->Data, (USHORT)actual_length));

        return irp->IoStatus.Status = STATUS_SUCCESS;
}

static NTSTATUS process_urb_res(struct urb_req *urbr, const struct usbip_header *hdr)
{
	IRP *irp = urbr->irp;
	if (!irp) {
		return STATUS_SUCCESS;
	}

	if (hdr->base.command != USBIP_RET_SUBMIT) {
		TraceError(TRACE_WRITE, "USBIP_RET_SUBMIT expected, got %!usbip_request_type!", hdr->base.command);
		return STATUS_INVALID_PARAMETER;
	}

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = internal_usb_submit_urb(urbr->vpdo, URB_FROM_IRP(irp), hdr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(urbr, hdr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = STATUS_SUCCESS;
		break;
	default:
		char buf[URB_REQ_STR_BUFSZ];
		TraceWarning(TRACE_WRITE, "Unhandled %s(%#08lX), %s", 
				dbg_ioctl_code(ioctl_code), ioctl_code, urb_req_str(buf, sizeof(buf), urbr));
	}

	return st;
}

static struct usbip_header *get_usbip_hdr_from_write_irp(IRP *irp)
{
	struct usbip_header *hdr = NULL;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG len = irpstack->Parameters.Write.Length;
	
	if (len >= sizeof(*hdr)) {
		irp->IoStatus.Information = len;
		hdr = irp->AssociatedIrp.SystemBuffer;
	}
	
	return hdr;
}

static void complete_irp(IRP *irp, NTSTATUS status)
{
	KIRQL oldirql;

	IoAcquireCancelSpinLock(&oldirql);
	bool valid_irp = IoSetCancelRoutine(irp, NULL);
	IoReleaseCancelSpinLock(oldirql);

	if (!valid_irp) {
		return;
	}

	irp->IoStatus.Status = status;

	/* it seems windows client usb driver will think
	* IoCompleteRequest is running at DISPATCH_LEVEL
	* so without this it will change IRQL sometimes,
	* and introduce to a dead of my userspace program
	*/
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	KeLowerIrql(oldirql);
}

static NTSTATUS process_write_irp(vpdo_dev_t *vpdo, IRP *write_irp)
{
	struct usbip_header *hdr = get_usbip_hdr_from_write_irp(write_irp);
	if (!hdr) {
		TraceError(TRACE_WRITE, "usbip header expected");
		return STATUS_INVALID_PARAMETER;
	}

	struct urb_req *urbr = find_sent_urbr(vpdo, hdr->base.seqnum);
	if (!urbr) {
		TraceInfo(TRACE_WRITE, "urb_req not found (cancelled?), seqnum %u", hdr->base.seqnum);
		return STATUS_SUCCESS;
	}

	IRP *irp = urbr->irp;

	{
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceInfo(TRACE_WRITE, "irp %#p -> %s", irp, dbg_usbip_hdr(buf, sizeof(buf), hdr));
	}

	NTSTATUS status = process_urb_res(urbr, hdr);
	free_urbr(urbr);

	if (irp) {
		complete_irp(irp, status);
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_write(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();

	TraceVerbose(TRACE_WRITE, "Enter irql %!irql!", KeGetCurrentIrql());

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		TraceWarning(TRACE_WRITE, "write for non-vhci is not allowed");
		return irp_done(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->common.DevicePnPState != Deleted) {

		IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
		vpdo_dev_t *vpdo = irpstack->FileObject->FsContext;
		
		if (vpdo && vpdo->plugged) {
			irp->IoStatus.Information = 0;
			status = process_write_irp(vpdo, irp);
		} else {
			TraceVerbose(TRACE_WRITE, "null or unplugged");
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	TraceVerbose(TRACE_WRITE, "Leave %!STATUS!", status);
	return irp_done(irp, status);
}
