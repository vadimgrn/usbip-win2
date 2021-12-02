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

static bool save_iso_desc(struct _URB_ISOCH_TRANSFER *urb, const struct usbip_iso_packet_descriptor *iso_desc)
{
	for (ULONG i = 0; i < urb->NumberOfPackets; ++i, ++iso_desc) {

		if (iso_desc->offset > urb->IsoPacket[i].Offset) {
			TraceWarning(TRACE_WRITE, "why offset changed?%d %d %d %d",
						i, iso_desc->offset, iso_desc->actual_length, urb->IsoPacket[i].Offset);
				
			return false;
		}

		urb->IsoPacket[i].Length = iso_desc->actual_length;
		urb->IsoPacket[i].Status = to_windows_status(iso_desc->status);
	}

	return true;
}

static void *get_buf(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}
	
	if (bufMDL) {
		buf = MmGetSystemAddressForMdlSafe(bufMDL, NormalPagePriority);
	}

	if (!buf) {
		TraceWarning(TRACE_WRITE, "No transfer buffer");
	}

	return buf;
}

static void copy_iso_data(char *dest, ULONG dest_len, char *src, ULONG src_len, struct _URB_ISOCH_TRANSFER *urb)
{
	ULONG offset = 0;

	for (ULONG i = 0; i < urb->NumberOfPackets; ++i) {
	
		if (!urb->IsoPacket[i].Length) {
			continue;
		}

		if (urb->IsoPacket[i].Offset + urb->IsoPacket[i].Length	> dest_len) {
			TraceWarning(TRACE_WRITE, "Warning, why this?");
			break;
		}

		if (offset + urb->IsoPacket[i].Length > src_len) {
			TraceWarning(TRACE_WRITE, "Warning, why that?");
			break;
		}

		RtlCopyMemory(dest + urb->IsoPacket[i].Offset, src + offset, urb->IsoPacket[i].Length);
		offset += urb->IsoPacket[i].Length;
	}

	if (offset != src_len) {
		TraceWarning(TRACE_WRITE, "why not equal offset:%d src_len:%d", offset, src_len);
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
	struct _URB_SELECT_INTERFACE *r = &urb->UrbSelectInterface;
	return vpdo_select_interface(vpdo, &r->Interface);
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

static NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	int actual_length = hdr->u.ret_submit.actual_length;

	{
		int in_len = is_endpoint_direction_in(r->PipeHandle) ? actual_length : 0;
		struct usbip_iso_packet_descriptor *d = (struct usbip_iso_packet_descriptor*)((char*)(hdr + 1) + in_len);
		if (!save_iso_desc(r, d)) {
			return STATUS_INVALID_PARAMETER;
		}
	}

	r->ErrorCount = hdr->u.ret_submit.error_count;

	if (r->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r->StartFrame = hdr->u.ret_submit.start_frame;
	}

	void *buf = get_buf(r->TransferBuffer, r->TransferBufferMDL);
	if (!buf) {
		return STATUS_INVALID_PARAMETER;
	}

	copy_iso_data(buf, r->TransferBufferLength, (char*)(hdr + 1), actual_length, r);
	r->TransferBufferLength = actual_length;

	return STATUS_SUCCESS;
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

	int linux_status = hdr->u.ret_submit.status;
	if (!linux_status) {
		return store_urb_data(vpdo, urb, hdr);
	}

	USBD_STATUS status = to_windows_status(linux_status);
	urb->UrbHeader.Status = status;

	if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
		urb->UrbBulkOrInterruptTransfer.TransferBufferLength = hdr->u.ret_submit.actual_length;
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

	req = urbr->irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(req->Data, hdr + 1, actual_length);
		
	char buf[DBG_USB_SETUP_BUFBZ];
	TraceVerbose(TRACE_WRITE, "ConnectionIndex %lu, SetupPacket %s, Data %!BIN!", 
				req->ConnectionIndex, 
				dbg_usb_setup_packet(buf, sizeof(buf), &req->SetupPacket),
				WppBinary(req->Data, (USHORT)actual_length));

        return irp->IoStatus.Status = STATUS_SUCCESS;
}

static NTSTATUS process_urb_res(struct urb_req *urbr, const struct usbip_header *hdr)
{
	if (!urbr->irp) {
		return STATUS_SUCCESS;
	}

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = internal_usb_submit_urb(urbr->vpdo, URB_FROM_IRP(urbr->irp), hdr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(urbr, hdr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = STATUS_SUCCESS;
		break;
	default:
		char buf[DBG_URBR_BUFSZ];
		TraceWarning(TRACE_WRITE, "Unhandled %s(%#08lX), urbr %s", 
			dbg_ioctl_code(ioctl_code), ioctl_code, dbg_urbr(buf, sizeof(buf), urbr));
	}

	return st;
}

static struct usbip_header *get_usbip_hdr_from_write_irp(IRP *irp)
{
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG len = irpstack->Parameters.Write.Length;
	
	if (len >= sizeof(struct usbip_header)) {
		irp->IoStatus.Information = len;
		return irp->AssociatedIrp.SystemBuffer;
	}
	
	return NULL;
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
	if (!urbr) { // might have been cancelled before, so return STATUS_SUCCESS
		TraceInfo(TRACE_WRITE, "urb_req not found, seqnum %u", hdr->base.seqnum);
		return STATUS_SUCCESS;
	}

	NTSTATUS status = process_urb_res(urbr, hdr);

	IRP *irp = urbr->irp;
	free_urbr(urbr);

	if (irp) {
		complete_irp(irp, status);
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_write(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceVerbose(TRACE_WRITE, "irql %!irql!, Length %lu", KeGetCurrentIrql(), irpstack->Parameters.Write.Length);

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		TraceWarning(TRACE_WRITE, "write for non-vhci is not allowed");
		return irp_done(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->common.DevicePnPState != Deleted) {

		vpdo_dev_t *vpdo = irpstack->FileObject->FsContext;
		
		if (vpdo && vpdo->plugged) {
			irp->IoStatus.Information = 0;
			status = process_write_irp(vpdo, irp);
		} else {
			TraceVerbose(TRACE_WRITE, "null or unplugged");
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	return irp_done(irp, status);
}
