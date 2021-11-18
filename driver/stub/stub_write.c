/* libusb-win32, Generic Windows USB Library
* Copyright (c) 2010 Travis Robinson <libusbdotnet@gmail.com>
* Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "stub_driver.h"
#include "stub_trace.h"
#include "stub_write.tmh"

#include "stub_dbg.h"
#include "usbip_proto.h"
#include "usbd_helper.h" 
#include "stub_usbd.h"
#include "stub_res.h"
#include "pdu.h"

#define HDR_IS_CONTROL_TRANSFER(hdr)	((hdr)->base.ep == 0)

static void
process_get_status(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	USHORT	op, idx = 0;
	USHORT	data;
	UCHAR	datalen;

	TraceInfo(TRACE_READWRITE, "get_status\n");

	switch (CSPKT_RECIPIENT(csp)) {
	case BMREQUEST_TO_DEVICE:
		op = URB_FUNCTION_GET_STATUS_FROM_DEVICE;
		break;
	case BMREQUEST_TO_INTERFACE:
		op = URB_FUNCTION_GET_STATUS_FROM_INTERFACE;
		idx = csp->wIndex.W;
		break;
	case BMREQUEST_TO_ENDPOINT:
		op = URB_FUNCTION_GET_STATUS_FROM_ENDPOINT;
		idx = csp->wIndex.W;
		break;
	default:
		op = URB_FUNCTION_GET_STATUS_FROM_OTHER;
		break;
	}
	if (get_usb_status(devstub, op, idx, &data, &datalen))
		reply_stub_req_data(devstub, seqnum, &data, (int)datalen, TRUE);
	else
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
}

static void
process_get_desc(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	UCHAR	descType = CSPKT_DESCRIPTOR_TYPE(csp);
	PVOID	pdesc = NULL;
	BOOLEAN	res;
	ULONG	len;

	TraceInfo(TRACE_READWRITE, "get_desc: %!usb_descriptor_type!", CSPKT_DESCRIPTOR_TYPE(csp));

	pdesc = ExAllocatePoolWithTag(NonPagedPool, csp->wLength, USBIP_STUB_POOL_TAG);
	if (pdesc == NULL) {
		TraceError(TRACE_READWRITE, "process_get_desc: out of memory");
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
		return;
	}

	len = csp->wLength;
	if (descType == 0x22) {
		/* NOTE: Try to tweak in a clumsy way.
		 * Windows gives an USBD_STATUS_STALL_PID for non-designated descriptor
		 * such as USBHID REPORT. With raw control transfer URB, it has no problem.
		 */
		res = submit_control_transfer(devstub, csp, pdesc, &len);
	}
	else {
		USHORT	idLang = 0;

		if (descType == USB_STRING_DESCRIPTOR_TYPE)
			idLang = csp->wIndex.W;
		res = get_usb_desc(devstub, descType, CSPKT_DESCRIPTOR_INDEX(csp), idLang, pdesc, &len);
	}
	if (!res) {
		TraceWarning(TRACE_READWRITE, "process_get_desc: failed to get descriptor\n");
		ExFreePool(pdesc);
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -32);
		return;
	}
	reply_stub_req_data(devstub, seqnum, pdesc, len, FALSE);
}

static void
process_clear_feature(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	PUSBD_PIPE_INFORMATION	info_pipe;

	TraceInfo(TRACE_READWRITE, "%!bmrequest_to!", CSPKT_RECIPIENT(csp));

	switch (CSPKT_RECIPIENT(csp)) {
	case BMREQUEST_TO_ENDPOINT:
		info_pipe = get_info_pipe(devstub->devconf, (UCHAR)csp->wIndex.W);
		if (info_pipe) {
			reset_pipe(devstub, info_pipe->PipeHandle);
			reply_stub_req_hdr(devstub, USBIP_RET_SUBMIT, seqnum);
		}
		else {
			TraceError(TRACE_READWRITE, "no such ep");
			reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
		}
		break;
	default:
		TraceError(TRACE_READWRITE, "not supported %!bmrequest_to!", CSPKT_RECIPIENT(csp));
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
		break;
	}
}

static void
process_set_feature(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	int	res;

	TraceInfo(TRACE_READWRITE, "%!bmrequest_to!", CSPKT_RECIPIENT(csp));

	switch (CSPKT_RECIPIENT(csp)) {
	case BMREQUEST_TO_DEVICE:
		res = set_feature(devstub, URB_FUNCTION_SET_FEATURE_TO_DEVICE, csp->wValue.W, csp->wIndex.W);
		break;
	case BMREQUEST_TO_ENDPOINT:
		res = set_feature(devstub, URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, csp->wValue.W, csp->wIndex.W);
		break;
	default:
		TraceError(TRACE_READWRITE, "not supported %!bmrequest_to!", CSPKT_RECIPIENT(csp));
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
		return;
	}
	if (res == 0)
		reply_stub_req_hdr(devstub, USBIP_RET_SUBMIT, seqnum);
	else {
		TraceInfo(TRACE_READWRITE, "failed to set feature\n");
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, res);
	}
}

static void
process_select_conf(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	if (select_usb_conf(devstub, csp->wValue.W))
		reply_stub_req_hdr(devstub, USBIP_RET_SUBMIT, seqnum);
	else
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
}

static void
process_select_intf(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	if (select_usb_intf(devstub, (UCHAR)csp->wIndex.W, csp->wValue.W))
		reply_stub_req_hdr(devstub, USBIP_RET_SUBMIT, seqnum);
	else
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, -1);
}

static void
process_standard_request(usbip_stub_dev_t *devstub, unsigned int seqnum, USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	switch (csp->bRequest) {
	case USB_REQUEST_GET_STATUS:
		process_get_status(devstub, seqnum, csp);
		break;
	case USB_REQUEST_GET_DESCRIPTOR:
		process_get_desc(devstub, seqnum, csp);
		break;
	case USB_REQUEST_CLEAR_FEATURE:
		process_clear_feature(devstub, seqnum, csp);
		break;
	case USB_REQUEST_SET_FEATURE:
		process_set_feature(devstub, seqnum, csp);
		break;
	case USB_REQUEST_SET_CONFIGURATION:
		process_select_conf(devstub, seqnum, csp);
		break;
	case USB_REQUEST_SET_INTERFACE:
		process_select_intf(devstub, seqnum, csp);
		break;
	default:
		TraceError(TRACE_READWRITE, "not supported standard request: %!bmrequest!", CSPKT_REQUEST_TYPE(csp));
		break;
	}
}

static void
process_class_vendor_request(usbip_stub_dev_t *devstub, USB_DEFAULT_PIPE_SETUP_PACKET *csp, struct usbip_header *hdr, BOOLEAN vendorreq)
{
	PVOID	data;
	ULONG	datalen;
	USHORT	cmd;
	UCHAR	reservedBits;
	unsigned long	seqnum;
	BOOLEAN	is_in;
	int	res;

	datalen = hdr->u.cmd_submit.transfer_buffer_length;
	is_in = hdr->base.direction ? TRUE : FALSE;
	if (datalen == 0)
		data = NULL;
	else {
		if (is_in) {
			data = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)datalen, USBIP_STUB_POOL_TAG);
			if (data == NULL) {
				TraceError(TRACE_GENERAL, "process_class_vendor_request: out of memory\n");
				reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
				return;
			}
		}
		else {
			data = (PVOID)(hdr + 1);
		}
	}

	switch (csp->bmRequestType.Recipient) {
	case BMREQUEST_TO_DEVICE:
		cmd = vendorreq ? URB_FUNCTION_VENDOR_DEVICE : URB_FUNCTION_CLASS_DEVICE;
		break;
	case BMREQUEST_TO_INTERFACE:
		cmd = vendorreq ? URB_FUNCTION_VENDOR_INTERFACE : URB_FUNCTION_CLASS_INTERFACE;
		break;
	case BMREQUEST_TO_ENDPOINT:
		cmd = vendorreq ? URB_FUNCTION_VENDOR_ENDPOINT : URB_FUNCTION_CLASS_ENDPOINT;
		break;
	default:
		cmd = vendorreq ? URB_FUNCTION_VENDOR_OTHER : URB_FUNCTION_CLASS_OTHER;
		break;
	}

	reservedBits = csp->bmRequestType.Reserved;
	seqnum = hdr->base.seqnum;
	res = submit_class_vendor_req(devstub, is_in, cmd, reservedBits, csp->bRequest, csp->wValue.W, csp->wIndex.W, data, &datalen);
	if (res == 0) {
		if (is_in) {
			reply_stub_req_data(devstub, seqnum, data, datalen, TRUE);
			if (data != NULL)
				ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
		}
		else
			reply_stub_req_hdr(devstub, USBIP_RET_SUBMIT, seqnum);
	}
	else {
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, seqnum, res);
		if (is_in && data != NULL)
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
	}
}

static void
process_control_transfer(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	char buf[DBG_USB_SETUP_BUFBZ];
	TraceInfo(TRACE_READWRITE, "seq %u, %s", hdr->base.seqnum, dbg_usb_setup_packet(buf, sizeof(buf), setup));

	UCHAR reqType = CSPKT_REQUEST_TYPE(setup);

	switch (reqType) {
	case BMREQUEST_STANDARD:
		process_standard_request(devstub, hdr->base.seqnum, setup);
		break;
	case BMREQUEST_CLASS:
		process_class_vendor_request(devstub, setup, hdr, FALSE);
		break;
	case BMREQUEST_VENDOR:
		process_class_vendor_request(devstub, setup, hdr, TRUE);
		break;
	default:
		TraceError(TRACE_READWRITE, "invalid request type: %!bmrequest!", reqType);
		break;
	}
}

static void
process_bulk_intr_transfer(usbip_stub_dev_t *devstub, PUSBD_PIPE_INFORMATION info_pipe, struct usbip_header *hdr)
{
	PVOID	data;
	ULONG	datalen;
	BOOLEAN	is_in;
	NTSTATUS	status;

	TraceInfo(TRACE_READWRITE, "bulk_intr_transfer: seq:%u, ep:%s\n", hdr->base.seqnum, dbg_info_pipe(info_pipe));

	datalen = (ULONG)hdr->u.cmd_submit.transfer_buffer_length;
	is_in = hdr->base.direction ? TRUE : FALSE;
	if (is_in) {
		data = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)datalen, USBIP_STUB_POOL_TAG);
		if (data == NULL) {
			TraceError(TRACE_GENERAL, "process_bulk_intr_transfer: out of memory\n");
			reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
			return;
		}
	}
	else {
		data = (PVOID)(hdr + 1);
	}

	status = submit_bulk_intr_transfer(devstub, info_pipe->PipeHandle, hdr->base.seqnum, data, datalen, is_in);
	if (NT_ERROR(status)) {
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
		if (is_in)
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
	}
}

static void
process_iso_transfer(usbip_stub_dev_t *devstub, PUSBD_PIPE_INFORMATION info_pipe, const struct usbip_header *hdr)
{
	TraceInfo(TRACE_READWRITE, "iso_transfer: seq:%u, ep:%s\n", hdr->base.seqnum, dbg_info_pipe(info_pipe));

	ULONG datalen = 0;
	PVOID data = NULL;
	struct usbip_iso_packet_descriptor *iso_descs = NULL;

	bool dir_in = hdr->base.direction == USBIP_DIR_IN;
	ULONG TransferFlags = to_windows_flags(hdr->u.cmd_submit.transfer_flags, dir_in);

	ULONG n_pkts = hdr->u.cmd_submit.number_of_packets;
	size_t iso_descs_len = sizeof(*iso_descs)*n_pkts;

	if (dir_in) {
		iso_descs = (struct usbip_iso_packet_descriptor*)(hdr + 1);
		datalen = get_iso_descs_len(n_pkts, iso_descs, FALSE);
		data = ExAllocatePoolWithTag(NonPagedPool, datalen + sizeof(*iso_descs)*n_pkts, USBIP_STUB_POOL_TAG);
		if (!data) {
			TraceError(TRACE_GENERAL, "process_iso_transfer: out of memory\n");
			reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
			return;
		}
		RtlCopyMemory((char*)data + datalen, iso_descs, iso_descs_len);
	} else {
		/* Allocate more space for iso descriptors which will maintain length field */
		datalen = hdr->u.cmd_submit.transfer_buffer_length;
		iso_descs = (struct usbip_iso_packet_descriptor*)((char*)(hdr + 1) + datalen);
		data = ExAllocatePoolWithTag(NonPagedPool, datalen + iso_descs_len, USBIP_STUB_POOL_TAG);
		if (!data) {
			TraceError(TRACE_GENERAL, "process_iso_transfer: out of memory\n");
			reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
			return;
		}
		RtlCopyMemory((char *)data, hdr + 1, datalen + iso_descs_len);
	}

	NTSTATUS status = submit_iso_transfer(devstub, info_pipe->PipeHandle, hdr->base.seqnum, TransferFlags, n_pkts,
		hdr->u.cmd_submit.start_frame, iso_descs, data, datalen);
	
	if (NT_ERROR(status)) {
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
		if (dir_in) {
			ExFreePoolWithTag(data, USBIP_STUB_POOL_TAG);
		}
	}
}

static UCHAR
get_epaddr_from_hdr(struct usbip_header *hdr)
{
	return (UCHAR)((hdr->base.direction ? USB_ENDPOINT_DIRECTION_MASK : 0) | hdr->base.ep);
}

static void
process_data_transfer(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	PUSBD_PIPE_INFORMATION	info_pipe;
	UCHAR	epaddr;

	epaddr = get_epaddr_from_hdr(hdr);
	info_pipe = get_info_pipe(devstub->devconf, epaddr);
	if (info_pipe == NULL) {
		TraceWarning(TRACE_READWRITE, "non-existent pipe: %x\n", (int)epaddr);
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
		return;
	}
	switch (info_pipe->PipeType) {
	case UsbdPipeTypeBulk:
	case UsbdPipeTypeInterrupt:
		process_bulk_intr_transfer(devstub, info_pipe, hdr);
		break;
	case UsbdPipeTypeIsochronous:
		process_iso_transfer(devstub, info_pipe, hdr);
		break;
	default:
		TraceError(TRACE_READWRITE, "not supported transfer type\n");
		break;
	}
}

static void
process_reset_pipe(usbip_stub_dev_t *devstub, struct usbip_header *hdr)
{
	PUSBD_PIPE_INFORMATION	info_pipe;
	UCHAR	epaddr;

	epaddr = get_epaddr_from_hdr(hdr);
	info_pipe = get_info_pipe(devstub->devconf, epaddr);
	if (info_pipe == NULL) {
		TraceWarning(TRACE_READWRITE, "non-existent pipe: %x\n", (int)epaddr);
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -1);
		return;
	}

	TraceInfo(TRACE_READWRITE, "pipeHandle = %p\n", info_pipe->PipeHandle);

	if (NT_SUCCESS(reset_pipe(devstub, info_pipe->PipeHandle)))
		reply_stub_req_data(devstub, hdr->base.seqnum, NULL, 0, FALSE);
	else
		reply_stub_req_err(devstub, USBIP_RET_SUBMIT, hdr->base.seqnum, -8);
}

static NTSTATUS
process_cmd_submit(usbip_stub_dev_t *devstub, PIRP irp, struct usbip_header *hdr)
{
	PIO_STACK_LOCATION	irpstack;

	if (HDR_IS_CONTROL_TRANSFER(hdr)) {
		process_control_transfer(devstub, hdr);
	}
	else {
		USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);
		if (CSPKT_REQUEST_TYPE(setup) == BMREQUEST_STANDARD && CSPKT_RECIPIENT(setup) == BMREQUEST_TO_ENDPOINT && CSPKT_REQUEST(setup) == USB_REQUEST_RESET_PIPE) {
			process_reset_pipe(devstub, hdr);
		} else {
			process_data_transfer(devstub, hdr);
		}
	}

	irpstack = IoGetCurrentIrpStackLocation(irp);

	irp->IoStatus.Information = irpstack->Parameters.Write.Length;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

static NTSTATUS
process_cmd_unlink(usbip_stub_dev_t *devstub, PIRP irp, struct usbip_header *hdr)
{
	PIO_STACK_LOCATION	irpstack;

	TraceInfo(TRACE_READWRITE, "process_cmd_unlink: enter\n");

	if (cancel_pending_stub_res(devstub, hdr->u.cmd_unlink.seqnum)) {
		reply_stub_req_hdr(devstub, USBIP_RET_UNLINK, hdr->base.seqnum);
	}
	else {
		reply_stub_req_err(devstub, USBIP_RET_UNLINK, hdr->base.seqnum, -1);
	}

	irpstack = IoGetCurrentIrpStackLocation(irp);

	irp->IoStatus.Information = irpstack->Parameters.Write.Length;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

static struct usbip_header *
get_usbip_hdr_from_write_irp(PIRP irp)
{
	PIO_STACK_LOCATION	irpstack;
	ULONG	len;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	len = irpstack->Parameters.Write.Length;
	if (len < sizeof(struct usbip_header)) {
		return NULL;
	}
	return (struct usbip_header *)irp->AssociatedIrp.SystemBuffer;
}

NTSTATUS
stub_dispatch_write(usbip_stub_dev_t *devstub, IRP *irp)
{
	struct usbip_header	*hdr;

	hdr = get_usbip_hdr_from_write_irp(irp);
	if (hdr == NULL) {
		TraceError(TRACE_READWRITE, "small write irp\n");
		return STATUS_INVALID_PARAMETER;
	}

	char buf[DBG_USBIP_HDR_BUFSZ];
	TraceInfo(TRACE_READWRITE, "hdr: %s", dbg_usbip_hdr(buf, sizeof(buf), hdr));

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		return process_cmd_submit(devstub, irp, hdr);
	case USBIP_CMD_UNLINK:
		return process_cmd_unlink(devstub, irp, hdr);
	default:
		TraceError(TRACE_READWRITE, "invalid command: %!usbip_request_type!\n", hdr->base.command);
		return STATUS_INVALID_PARAMETER;
	}
}
