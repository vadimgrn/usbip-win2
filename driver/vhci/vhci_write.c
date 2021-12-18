#include "vhci.h"
#include "trace.h"
#include "vhci_write.tmh"

#include "dbgcommon.h"
#include "usbip_proto.h"
#include "vhci_vpdo.h"
#include "vhci_vpdo_dsc.h"
#include "usbreq.h"
#include "usbd_helper.h"
#include "vhci_irp.h"

/*
 * These URBs have the same layout from the beginning of the structure.
 * Pointer to any of them can be used to access TransferBufferLength, TransferBuffer, TransferBufferMDL of any other instance 
 * of these URBs.
 */
// const size_t off = offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength);
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferLength), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferLength) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferLength), "assert");

static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBuffer), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBuffer) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBuffer), "assert");

static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_TRANSFER_EX, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_ISOCH_TRANSFER, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_STATUS_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferMDL), "assert");
static_assert(offsetof(struct _URB_CONTROL_TRANSFER, TransferBufferMDL) == offsetof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferMDL), "assert");

#define TRANSFERRED(irp) ((irp)->IoStatus.Information)

static PAGEABLE size_t get_ret_submit_pdu_size(const struct usbip_header *hdr)
{
	const struct usbip_header_ret_submit *r = &hdr->u.ret_submit;
	int data_len = hdr->base.direction == USBIP_DIR_OUT ? 0 : r->actual_length; 
	
	return data_len >= 0 && r->number_of_packets >= 0 ? 
		sizeof(*hdr) + data_len + r->number_of_packets*sizeof(struct usbip_iso_packet_descriptor) : 0;
}

static PAGEABLE NTSTATUS assignTransferBufferLength(ULONG *TransferBufferLength, int actual_length)
{
	bool ok = actual_length >= 0 && (ULONG)actual_length <= *TransferBufferLength;
	*TransferBufferLength = ok ? actual_length : 0;

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * Any struct with TransferBufferLength can be used.
 */
static PAGEABLE __inline NTSTATUS setTransferBufferLength(URB *urb, int actual_length)
{
	return assignTransferBufferLength(&urb->UrbControlTransfer.TransferBufferLength, actual_length);
}

static PAGEABLE __inline NTSTATUS ptr_to_status(const void *p)
{
	return p ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static PAGEABLE void *get_buf(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}

	if (!bufMDL) {
		TraceError(TRACE_WRITE, "TransferBuffer and TransferBufferMDL are NULL");
		return NULL;
	}

	buf = MmGetSystemAddressForMdlSafe(bufMDL, NormalPagePriority | MdlMappingNoExecute);
	if (!buf) {
		TraceError(TRACE_WRITE, "MmGetSystemAddressForMdlSafe error");
	}

	return buf;
}

/*
 * TransferBufferLength must already be set by assignTransferBufferLength/setTransferBufferLength.
 */
static PAGEABLE void *copy_to_transfer_buffer(URB *urb, const void *src)
{
	bool mdl = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
	NT_ASSERT(urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used
	void *buf = get_buf(mdl ? NULL : r->TransferBuffer, r->TransferBufferMDL);

	if (buf) {
		RtlCopyMemory(buf, src, r->TransferBufferLength);
	} else {
		r->TransferBufferLength = 0;
	}

	return buf;
}

static PAGEABLE void *copy_to_transfer_buffer_length(URB *urb, const void *src, int actual_length)
{
	return setTransferBufferLength(urb, actual_length) == STATUS_SUCCESS ? copy_to_transfer_buffer(urb, src) : NULL;
}

static PAGEABLE NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : vpdo_select_config(vpdo, &urb->UrbSelectConfiguration);
}

static PAGEABLE NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	return hdr->u.ret_submit.status ? STATUS_UNSUCCESSFUL : vpdo_select_interface(vpdo, &urb->UrbSelectInterface);
}

static PAGEABLE NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	NTSTATUS st = assignTransferBufferLength(&r->TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (st) {
		return st;
	}

	switch (r->Hdr.Function) {
	case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
		return STATUS_SUCCESS; // USB_DIR_OUT
	}

	const USB_COMMON_DESCRIPTOR *dsc = NULL;

	if (r->TransferBufferLength < sizeof(*dsc)) {
		TraceError(TRACE_WRITE, "Descriptor is shorter than header: TransferBufferLength(%lu) < %Iu", 
					r->TransferBufferLength, sizeof(*dsc));

		r->TransferBufferLength = 0;
		return STATUS_INVALID_PARAMETER;
	}

	dsc = copy_to_transfer_buffer(urb, hdr + 1);
	if (!dsc) {
		return ptr_to_status(dsc);
	}

	if (r->TransferBufferLength < dsc->bLength) {
		TraceError(TRACE_WRITE, "TransferBufferLength(%lu) < bLength(%d)", r->TransferBufferLength, dsc->bLength);
		return STATUS_INVALID_PARAMETER;
	}

	TraceInfo(TRACE_WRITE, "%s: bLength %d, %!usb_descriptor_type!, %!BIN!", 
				urb_function_str(r->Hdr.Function), 
				dsc->bLength,
				dsc->bDescriptorType,
				WppBinary(dsc, (USHORT)r->TransferBufferLength));

	if (!urb->UrbHeader.Status) {
		try_to_cache_descriptor(vpdo, r, dsc);
	}

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_control_get_status_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_STATUS_REQUEST *r = &urb->UrbControlGetStatusRequest;

	void *buf = copy_to_transfer_buffer_length(urb, hdr + 1, hdr->u.ret_submit.actual_length);
	if (buf) {
		TraceInfo(TRACE_WRITE, "%!BIN!", WppBinary(buf, (USHORT)r->TransferBufferLength));
	}
	
	return ptr_to_status(buf);
}

static PAGEABLE NTSTATUS do_control_transfer(URB *urb, ULONG TransferFlags, ULONG *TransferBufferLength, const struct usbip_header *hdr)
{
	NTSTATUS st = assignTransferBufferLength(TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (st) {
		return st;
	}

	if (IsTransferDirectionOut(TransferFlags)) {
		return STATUS_SUCCESS;
	}

	void *buf = copy_to_transfer_buffer(urb, hdr + 1);
	if (buf) {
		TraceInfo(TRACE_WRITE, "%!BIN!", WppBinary(buf, (USHORT)*TransferBufferLength));
	}

	return ptr_to_status(buf);
}

static PAGEABLE NTSTATUS urb_control_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;
	return do_control_transfer(urb, r->TransferFlags, &r->TransferBufferLength, hdr);
}

static PAGEABLE NTSTATUS urb_control_transfer_ex(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header* hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER_EX	*r = &urb->UrbControlTransferEx;
	return do_control_transfer(urb, r->TransferFlags, &r->TransferBufferLength, hdr);
}

static PAGEABLE NTSTATUS urb_control_vendor_class_request(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;

	NTSTATUS st = assignTransferBufferLength(&r->TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (st) {
		return st;
	}

	if (IsTransferDirectionOut(r->TransferFlags)) {
		return STATUS_SUCCESS;
	}

	void *buf = copy_to_transfer_buffer(urb, hdr + 1);
	if (buf) {
		TraceInfo(TRACE_WRITE, "%!BIN!", WppBinary(buf, (USHORT)r->TransferBufferLength));
	}

	return ptr_to_status(buf);
}

static PAGEABLE NTSTATUS urb_bulk_or_interrupt_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;

	NTSTATUS st = assignTransferBufferLength(&r->TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (st) {
		return st;
	}

	if (IsTransferDirectionOut(r->TransferFlags)) {
		return STATUS_SUCCESS;
	}

	void *buf = copy_to_transfer_buffer(urb, hdr + 1);
	return ptr_to_status(buf);
}

/*
 * Buffer from the server has no gaps (compacted), SUM(src->actual_length) == src_len, 
 * src->offset is ignored for that reason.
 *
 * For isochronous packets: actual length is the sum of
 * the actual length of the individual, packets, but as
 * the packet offsets are not changed there will be
 * padding between the packets. To optimally use the
 * bandwidth the padding is not transmitted.
 *
 * See: 
 * <linux>/drivers/usb/usbip/stub_tx.c, stub_send_ret_submit
 * <linux>/drivers/usb/usbip/usbip_common.c, usbip_pad_iso

 */
static PAGEABLE NTSTATUS copy_isoch(
	struct _URB_ISOCH_TRANSFER *r, char *dst_buf, 
	const struct usbip_iso_packet_descriptor *src, const char *src_buf, ULONG src_len)
{
	bool dir_out = !src_buf;
	USBD_ISO_PACKET_DESCRIPTOR *dst = r->IsoPacket;
	
	ULONG src_offset = r->NumberOfPackets ? src->offset : 0;
	NT_ASSERT(!src_offset);

	for (ULONG i = 0; i < r->NumberOfPackets; ++i, ++src, ++dst) {
	
		dst->Status = src->status ? to_windows_status(src->status) : USBD_STATUS_SUCCESS;
		
		if (dir_out) {
			continue; // dst->Length not used for OUT transfers
		}

		if (!src->actual_length) {
			dst->Length = 0;
			continue;
		}

		if (src->actual_length > src->length) {
			TraceError(TRACE_WRITE, "src->actual_length(%u) > src->length(%u)", src->actual_length, src->length);
			return STATUS_INVALID_PARAMETER;
		}

		if (src->offset != dst->Offset) { // buffer is compacted, but offsets are intact
			TraceError(TRACE_WRITE, "src->offset(%u) != dst->Offset(%lu)", src->offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset > dst->Offset) {// source buffer has no gaps
			TraceError(TRACE_WRITE, "src_offset(%lu) > dst->Offset(%lu)", src_offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset + src->actual_length > src_len) {
			TraceError(TRACE_WRITE, "src_offset(%lu) + src->actual_length(%u) > src_len(%lu)", 
						src_offset, src->actual_length, src_len);

			return STATUS_INVALID_PARAMETER;
		}

		if (dst->Offset + src->actual_length > r->TransferBufferLength) {
			TraceError(TRACE_WRITE, "dst->Offset(%lu) + src->actual_length(%u) > r->TransferBufferLength(%lu)", 
				dst->Offset, src->actual_length, r->TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}

		RtlCopyMemory(dst_buf + dst->Offset, src_buf + src_offset, src->actual_length);
		
		dst->Length = src->actual_length;
		src_offset += src->actual_length;
	}

	bool ok = src_offset == src_len;
	if (!ok) {
		TraceError(TRACE_WRITE, "src_offset(%lu) != src_len(%lu)", src_offset, src_len);
	}

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * Layout: usbip_header, data(IN only), usbip_iso_packet_descriptor[].
 */
static PAGEABLE NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	const struct usbip_header_ret_submit *res = &hdr->u.ret_submit;

	int cnt = res->number_of_packets;

	if (!(cnt >= 0 && (ULONG)cnt == r->NumberOfPackets)) {
		TraceError(TRACE_WRITE, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r->NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}

	if (!(res->actual_length >= 0 && (ULONG)res->actual_length <= r->TransferBufferLength)) {
		TraceError(TRACE_WRITE, "actual_length(%d) > TransferBufferLength(%lu)", res->actual_length, r->TransferBufferLength);
		return STATUS_INVALID_PARAMETER;
	}

	r->ErrorCount = res->error_count;

	if (cnt && cnt == res->error_count) {
		r->Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r->StartFrame = res->start_frame;
	}

	bool dir_in = IsTransferDirectionIn(r->TransferFlags);

	const char *src_buf = (char*)(hdr + 1);
	ULONG src_len = dir_in ? res->actual_length : 0;

	const struct usbip_iso_packet_descriptor *src = (void*)(src_buf + src_len);

	void *buf_a = r->Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? NULL : r->TransferBuffer;
	void *buf = get_buf(buf_a, r->TransferBufferMDL);
	
	return buf ? copy_isoch(r, buf, src, dir_in ? src_buf : NULL, src_len) : 
		     ptr_to_status(buf);
}

static PAGEABLE NTSTATUS get_configuration(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_CONFIGURATION_REQUEST *r = &urb->UrbControlGetConfigurationRequest;

	void *buf = copy_to_transfer_buffer_length(urb, hdr + 1, hdr->u.ret_submit.actual_length);
	if (buf) {
		TraceInfo(TRACE_WRITE, "%!BIN!", WppBinary(buf, (USHORT)r->TransferBufferLength));
	}

	return ptr_to_status(buf);
}

static PAGEABLE NTSTATUS get_interface(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_INTERFACE_REQUEST *r = &urb->UrbControlGetInterfaceRequest;

	void *buf = copy_to_transfer_buffer_length(urb, hdr + 1, hdr->u.ret_submit.actual_length);
	if (buf) {
		TraceInfo(TRACE_WRITE, "Interface %hu alternate setting %!BIN!", 
				r->Interface, WppBinary(buf, (USHORT)r->TransferBufferLength));
	}

	return ptr_to_status(buf);
}

/*
* Nothing to handle.
*/
static PAGEABLE NTSTATUS urb_function_success(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(urb);
	UNREFERENCED_PARAMETER(hdr);
	
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_function_unexpected(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(hdr);

	USHORT func = urb->UrbHeader.Function;
	TraceError(TRACE_WRITE, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	return STATUS_INTERNAL_ERROR;
}	

typedef NTSTATUS (*urb_function_t)(vpdo_dev_t*, URB*, const struct usbip_header*);

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

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE 

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT, urb_control_feature_request

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

	urb_function_success, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL, urb_pipe_request

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_OTHER
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_OTHER

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_OTHER, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER, urb_control_feature_request

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	get_configuration, // URB_FUNCTION_GET_CONFIGURATION
	get_interface, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

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

/*
 * URB function must:
 * 1.Take into account UrbHeader.Status which was mapped from ret_submit.status.
 *   For example, select_configuration/select_interface immediately return STATUS_UNSUCCESSFUL in such case.
 * 2.If response from a server has data (actual_length > 0), the function MUST copy it to URB 
 *   even if UrbHeader.Status != STATUS_SUCCESS. Data was sent over the network, do not ditch it.
 */
static PAGEABLE NTSTATUS internal_usb_submit_urb(vpdo_dev_t *vpdo, URB *urb, const struct usbip_header *hdr)
{
	if (urb) {
		int err = hdr->u.ret_submit.status;
		urb->UrbHeader.Status = err ? to_windows_status(err) : USBD_STATUS_SUCCESS;
	} else {
		return STATUS_INVALID_PARAMETER;
	}

	USHORT func = urb->UrbHeader.Function;
	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : NULL;

	NTSTATUS st = pfunc ? pfunc(vpdo, urb, hdr) : STATUS_INVALID_PARAMETER;
	
	if (st && !urb->UrbHeader.Status) {
		urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
		TraceWarning(TRACE_WRITE, "Set USBD_STATUS=%s because return is %!STATUS!", 
					dbg_usbd_status(urb->UrbHeader.Status), st);
	} else if (urb->UrbHeader.Status && !st) {
		st = STATUS_UNSUCCESSFUL;
		TraceWarning(TRACE_WRITE, "Return %!STATUS! because USBD_STATUS=%s(%#08lX)", 
					st, dbg_usbd_status(urb->UrbHeader.Status), (ULONG)urb->UrbHeader.Status);
	}

	return st;
}

static PAGEABLE NTSTATUS get_descriptor_from_node_connection(struct urb_req *urbr, const struct usbip_header *hdr)
{
	int usbip_status = hdr->u.ret_submit.status;
	if (usbip_status) {
		USBD_STATUS st = to_windows_status(usbip_status);
		TraceError(TRACE_WRITE, "errno %d -> %s(%#08lX)", usbip_status, dbg_usbd_status(st), (ULONG)st);
		return STATUS_UNSUCCESSFUL;
	}

	IRP *irp = urbr->irp;
        IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	USB_DESCRIPTOR_REQUEST *req = NULL;
	int actual_length = hdr->u.ret_submit.actual_length;

	ULONG sz = actual_length + sizeof(*req);
	TRANSFERRED(irp) = sz;

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

static PAGEABLE NTSTATUS process_urb_res(struct urb_req *urbr, const struct usbip_header *hdr)
{
	IRP *irp = urbr->irp;
	if (!irp) {
		return STATUS_SUCCESS;
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

static PAGEABLE const struct usbip_header *consume_ret_submit_from_write_irp(IRP *irp)
{
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	const struct usbip_header *hdr = irp->AssociatedIrp.SystemBuffer;
	ULONG len = irpstack->Parameters.Write.Length;

	if (len >= sizeof(*hdr)) {
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceInfo(TRACE_WRITE, "IN[%u] %s", hdr->base.seqnum, dbg_usbip_hdr(buf, sizeof(buf), hdr));
	} else {
		TraceError(TRACE_WRITE, "usbip header expected: Write.Length(%lu) < %Iu", len, sizeof(*hdr));
		return NULL;
	}

	if (hdr->base.command != USBIP_RET_SUBMIT) {
		TraceError(TRACE_WRITE, "USBIP_RET_SUBMIT expected, got %!usbip_request_type!", hdr->base.command);
		return NULL;
	}

	size_t expected = get_ret_submit_pdu_size(hdr); 
	if (len == expected) {
		TRANSFERRED(irp) = len; // fully transferred
		return hdr;
	}
	
	TraceError(TRACE_WRITE, "Write.Length(%lu) != %Iu(hdr(%Iu) + in(%d)*actual_length(%d) + isoc(%Iu)[%d])", 
				len, expected, sizeof(*hdr), 
				hdr->base.direction == USBIP_DIR_IN, 
				hdr->u.ret_submit.actual_length, 
				sizeof(struct usbip_iso_packet_descriptor),
				hdr->u.ret_submit.number_of_packets);

	return NULL;
}

static PAGEABLE void complete_irp(IRP *irp, NTSTATUS status)
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

static PAGEABLE NTSTATUS process_write_irp(vpdo_dev_t *vpdo, IRP *write_irp)
{
	const struct usbip_header *hdr = consume_ret_submit_from_write_irp(write_irp);
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	struct urb_req *urbr = find_sent_urbr(vpdo, hdr->base.seqnum);
	if (!urbr) {
		TraceInfo(TRACE_WRITE, "urb_req not found (cancelled?), seqnum %u", hdr->base.seqnum);
		return STATUS_SUCCESS;
	}
	
	IRP *irp = urbr->irp;

	NTSTATUS status = process_urb_res(urbr, hdr);
	free_urbr(urbr);

	if (irp) {
		complete_irp(irp, status);
	}

	return STATUS_SUCCESS;
}

/*
* WriteFile -> IRP_MJ_WRITE -> vhci_write
*/
PAGEABLE NTSTATUS vhci_write(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(!TRANSFERRED(irp));

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
			status = process_write_irp(vpdo, irp);
		} else {
			TraceVerbose(TRACE_WRITE, "null or unplugged");
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	TraceVerbose(TRACE_WRITE, "Leave %!STATUS!", status);
	return irp_done(irp, status);
}
