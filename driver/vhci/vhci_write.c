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

static BOOLEAN
save_iso_desc(struct _URB_ISOCH_TRANSFER *urb, struct usbip_iso_packet_descriptor *iso_desc)
{
	ULONG	i;

	for (i = 0; i < urb->NumberOfPackets; i++) {
		if (iso_desc->offset > urb->IsoPacket[i].Offset) {
			TraceWarning(TRACE_WRITE, "why offset changed?%d %d %d %d",
			     i, iso_desc->offset, iso_desc->actual_length, urb->IsoPacket[i].Offset);
			return FALSE;
		}
		urb->IsoPacket[i].Length = iso_desc->actual_length;
		urb->IsoPacket[i].Status = to_windows_status(iso_desc->status);
		iso_desc++;
	}
	return TRUE;
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

static void
copy_iso_data(char *dest, ULONG dest_len, char *src, ULONG src_len, struct _URB_ISOCH_TRANSFER *urb)
{
	ULONG	i;
	ULONG	offset = 0;

	for (i = 0; i < urb->NumberOfPackets; i++) {
		if (urb->IsoPacket[i].Length == 0)
			continue;

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

void post_get_desc(vpdo_dev_t *vpdo, URB *urb)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *req = &urb->UrbControlDescriptorRequest;

	USB_COMMON_DESCRIPTOR *dsc = get_buf(req->TransferBuffer, req->TransferBufferMDL);
	if (!dsc) {
		return;
	}

	if (req->TransferBufferLength >= dsc->bLength) {
		try_to_cache_descriptor(vpdo, req, dsc);
	} else {
		TraceInfo(TRACE_WRITE, "skip to cache partial descriptor: (%u < %d)", req->TransferBufferLength, (int)dsc->bLength);
	}

}

static NTSTATUS post_select_config(vpdo_dev_t *vpdo, URB *urb)
{
	return vpdo_select_config(vpdo, &urb->UrbSelectConfiguration);
}

static NTSTATUS post_select_interface(vpdo_dev_t *vpdo, URB *urb)
{
	struct _URB_SELECT_INTERFACE *urb_seli = &urb->UrbSelectInterface;
	return vpdo_select_interface(vpdo, &urb_seli->Interface);
}

static NTSTATUS copy_to_transfer_buffer(PVOID buf_dst, PMDL bufMDL, int dst_len, const void *src, int src_len)
{
	if (dst_len < src_len) {
		TraceError(TRACE_WRITE, "too small buffer: dest: %d, src: %d", dst_len, src_len);
		return STATUS_INVALID_PARAMETER;
	}

	void *buf = get_buf(buf_dst, bufMDL);
	if (buf) {
		RtlCopyMemory(buf, src, src_len);
		return STATUS_SUCCESS;
	}

	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS
store_urb_get_desc(PURB urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *urb_desc = &urb->UrbControlDescriptorRequest;
	NTSTATUS	status;

	status = copy_to_transfer_buffer(urb_desc->TransferBuffer, urb_desc->TransferBufferMDL,
		urb_desc->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_desc->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}

static NTSTATUS
store_urb_get_status(PURB urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_GET_STATUS_REQUEST	*urb_gsr = &urb->UrbControlGetStatusRequest;
	NTSTATUS	status;

	status = copy_to_transfer_buffer(urb_gsr->TransferBuffer, urb_gsr->TransferBufferMDL,
		urb_gsr->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_gsr->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}

static NTSTATUS
store_urb_control_transfer(PURB urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_TRANSFER	*urb_desc = &urb->UrbControlTransfer;
	NTSTATUS	status;

	if (urb_desc->TransferBufferLength == 0)
		return STATUS_SUCCESS;
	status = copy_to_transfer_buffer(urb_desc->TransferBuffer, urb_desc->TransferBufferMDL,
		urb_desc->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_desc->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}

static NTSTATUS
store_urb_control_transfer_ex(PURB urb, const struct usbip_header* hdr)
{
	struct _URB_CONTROL_TRANSFER_EX	*urb_desc = &urb->UrbControlTransferEx;
	NTSTATUS	status;

	status = copy_to_transfer_buffer(urb_desc->TransferBuffer, urb_desc->TransferBufferMDL,
		urb_desc->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_desc->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}

static NTSTATUS
store_urb_vendor_or_class(PURB urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vendor_class = &urb->UrbControlVendorClassRequest;

	if (IsTransferDirectionIn(urb_vendor_class->TransferFlags)) {
		NTSTATUS	status;
		status = copy_to_transfer_buffer(urb_vendor_class->TransferBuffer, urb_vendor_class->TransferBufferMDL,
			urb_vendor_class->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
		if (status == STATUS_SUCCESS)
			urb_vendor_class->TransferBufferLength = hdr->u.ret_submit.actual_length;
		return status;
	}
	else {
		urb_vendor_class->TransferBufferLength = hdr->u.ret_submit.actual_length;
	}
	return STATUS_SUCCESS;
}

static NTSTATUS store_urb_bulk_or_interrupt(URB *urb, const struct usbip_header *hdr)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER *urb_bi = &urb->UrbBulkOrInterruptTransfer;

	if (is_endpoint_direction_out(urb_bi->PipeHandle)) {
		return STATUS_SUCCESS;
	}

	NTSTATUS status = copy_to_transfer_buffer(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL,
		urb_bi->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);

	if (status == STATUS_SUCCESS) {
		urb_bi->TransferBufferLength = hdr->u.ret_submit.actual_length;
	}

	return status;
}

static NTSTATUS store_urb_iso(URB *urb, const struct usbip_header *hdr)
{
	struct _URB_ISOCH_TRANSFER *urb_iso = &urb->UrbIsochronousTransfer;
	INT32 in_len = is_endpoint_direction_in(urb_iso->PipeHandle) ? hdr->u.ret_submit.actual_length : 0;

	struct usbip_iso_packet_descriptor *iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + in_len);
	if (!save_iso_desc(urb_iso, iso_desc)) {
		return STATUS_INVALID_PARAMETER;
	}

	urb_iso->ErrorCount = hdr->u.ret_submit.error_count;

	void *buf = get_buf(urb_iso->TransferBuffer, urb_iso->TransferBufferMDL);
	if (!buf) {
		return STATUS_INVALID_PARAMETER;
	}

	copy_iso_data(buf, urb_iso->TransferBufferLength, (char *)(hdr + 1), hdr->u.ret_submit.actual_length, urb_iso);
	urb_iso->TransferBufferLength = hdr->u.ret_submit.actual_length;

	return STATUS_SUCCESS;
}

static NTSTATUS
store_urb_data(PURB urb, const struct usbip_header *hdr)
{
	NTSTATUS status = STATUS_SUCCESS;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urb_iso(urb, hdr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urb_bulk_or_interrupt(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urb_control_transfer(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urb_control_transfer_ex(urb, hdr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urb_get_desc(urb, hdr);
		break;
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = store_urb_get_status(urb, hdr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		status = store_urb_vendor_or_class(urb, hdr);
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
	case URB_FUNCTION_SELECT_INTERFACE:
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		break;
	default:
		TraceError(TRACE_WRITE, "%s: not supported", urb_function_str(urb->UrbHeader.Function));
		status = STATUS_INVALID_PARAMETER;
	}

	if (status == STATUS_SUCCESS) { // FIXME: ???
		urb->UrbHeader.Status = to_windows_status(hdr->u.ret_submit.status);
	}

	return status;
}

static NTSTATUS
process_urb_res_submit(pvpdo_dev_t vpdo, PURB urb, const struct usbip_header *hdr)
{
	if (!urb) {
		return STATUS_INVALID_PARAMETER;
	}

	if (hdr->u.ret_submit.status) {
		urb->UrbHeader.Status = to_windows_status(hdr->u.ret_submit.status);
		if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
			urb->UrbBulkOrInterruptTransfer.TransferBufferLength = hdr->u.ret_submit.actual_length;
		}

		USBD_STATUS st = urb->UrbHeader.Status;
		TraceError(TRACE_WRITE, "%s: %s(%#08lX)", urb_function_str(urb->UrbHeader.Function), dbg_usbd_status(st), (ULONG)st);
		return STATUS_UNSUCCESSFUL;
	}

	NTSTATUS status = store_urb_data(urb, hdr);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
		post_get_desc(vpdo, urb);
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		status = post_select_config(vpdo, urb);
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		status = post_select_interface(vpdo, urb);
		break;
	}

	return status;
}

static NTSTATUS
process_urb_dsc_req(struct urb_req *urbr, const struct usbip_header *hdr)
{
	if (hdr->u.ret_submit.status) {
		USBD_STATUS st = to_windows_status(hdr->u.ret_submit.status);
		TraceError(TRACE_WRITE, "%s(%#08lX)", dbg_usbd_status(st), (ULONG)st);
		return STATUS_UNSUCCESSFUL;
	}

	IRP *irp = urbr->irp;
        IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
        ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

        ULONG sz = hdr->u.ret_submit.actual_length + sizeof(USB_DESCRIPTOR_REQUEST);
        irp->IoStatus.Information = sz;
	
        if (outlen >= sz) {
                USB_DESCRIPTOR_REQUEST* dsc_req = urbr->irp->AssociatedIrp.SystemBuffer;
                RtlCopyMemory(dsc_req->Data, hdr + 1, hdr->u.ret_submit.actual_length);
                irp->IoStatus.Status = STATUS_SUCCESS;
        } else {
                irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
	}

	return irp->IoStatus.Status;
}

static NTSTATUS process_urb_res(struct urb_req *urbr, const struct usbip_header *hdr)
{
	if (!urbr->irp) {
		return STATUS_SUCCESS;
	}

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	{
		char buf[DBG_URBR_BUFSZ];
		TraceInfo(TRACE_WRITE, "urbr:%s, %s(%#08lX)", dbg_urbr(buf, sizeof(buf), urbr), dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = process_urb_res_submit(urbr->vpdo, URB_FROM_IRP(urbr->irp), hdr);
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = STATUS_SUCCESS;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = process_urb_dsc_req(urbr, hdr);
	default:
		TraceError(TRACE_WRITE, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
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
		TraceError(TRACE_WRITE, "too small");
		return STATUS_INVALID_PARAMETER;
	}

	struct urb_req *urbr = find_sent_urbr(vpdo, hdr);
	if (!urbr) { // might have been cancelled before, so return STATUS_SUCCESS
		TraceError(TRACE_WRITE, "no urbr: seqnum: %d", hdr->base.seqnum);
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
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	return irp_done(irp, status);
}
