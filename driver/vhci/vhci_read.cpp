#include "vhci_read.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_read.tmh"

#include "vhci_irp.h"
#include "vhci_proto.h"
#include "usbd_helper.h"
#include "pdu.h"
#include "ch9.h"
#include "ch11.h"

#define TRANSFERRED(irp) ((irp)->IoStatus.Information)

static inline auto get_irp_buffer(const IRP *read_irp)
{
	return read_irp->AssociatedIrp.SystemBuffer;
}

static ULONG get_irp_buffer_size(const IRP *read_irp)
{
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation((IRP*)read_irp);
	return irpstack->Parameters.Read.Length;
}

static void *try_get_irp_buffer(const IRP *irp, size_t min_size)
{
	NT_ASSERT(!TRANSFERRED(irp));

	ULONG sz = get_irp_buffer_size(irp);
	return sz >= min_size ? get_irp_buffer(irp) : nullptr;
}

static const void *get_urb_buffer(void *buf, MDL *bufMDL)
{
	if (buf) {
		return buf;
	}

	if (!bufMDL) {
		Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are nullptr");
		return nullptr;
	}

	buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority | MdlMappingNoExecute | MdlMappingNoWrite);
	if (!buf) {
		Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
	}

	return buf;
}

/*
 * PAGED_CODE() fails.
 * USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
 */
static NTSTATUS do_copy_payload(void *dst_buf, const struct _URB_ISOCH_TRANSFER *r, ULONG *transferred)
{
	NT_ASSERT(dst_buf);

	*transferred = 0;
	bool mdl = r->Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;

	const void *src_buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);
	if (!src_buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	ULONG buf_sz = is_endpoint_direction_out(r->PipeHandle) ? r->TransferBufferLength : 0; // TransferFlags can have wrong direction

	RtlCopyMemory(dst_buf, src_buf, buf_sz);
	*transferred += buf_sz;

	auto dsc = reinterpret_cast<usbip_iso_packet_descriptor*>((char*)dst_buf + buf_sz);
	ULONG sum = 0;

	for (ULONG i = 0; i < r->NumberOfPackets; ++dsc) {

		ULONG offset = r->IsoPacket[i].Offset;
		ULONG next_offset = ++i < r->NumberOfPackets ? r->IsoPacket[i].Offset : r->TransferBufferLength;

		if (next_offset >= offset && next_offset <= r->TransferBufferLength) {
			dsc->offset = offset;
			dsc->length = next_offset - offset;
			dsc->actual_length = 0;
			dsc->status = 0;
			sum += dsc->length;
		} else {
			Trace(TRACE_LEVEL_ERROR, "[%lu] next_offset(%lu) >= offset(%lu) && next_offset <= r->TransferBufferLength(%lu)",
						i, next_offset, offset, r->TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}
	}

	*transferred += r->NumberOfPackets*sizeof(*dsc);

	NT_ASSERT(sum == r->TransferBufferLength);
	return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 */
static ULONG get_payload_size(const struct _URB_ISOCH_TRANSFER *r)
{
	ULONG len = r->NumberOfPackets*sizeof(struct usbip_iso_packet_descriptor);

	if (is_endpoint_direction_out(r->PipeHandle)) {
		len += r->TransferBufferLength;
	}

	return len;
}

static NTSTATUS do_copy_transfer_buffer(void *dst, const URB *urb, IRP *irp)
{
	NT_ASSERT(dst);

	bool mdl = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
	NT_ASSERT(urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

	const struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used

	const void *buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(dst, buf, r->TransferBufferLength);
		TRANSFERRED(irp) += r->TransferBufferLength;
	}

	return buf ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/*
* PAGED_CODE() fails.
*/
static NTSTATUS copy_payload(void *dst, IRP *irp, const struct _URB_ISOCH_TRANSFER *r, ULONG expected)
{
	UNREFERENCED_PARAMETER(expected);

	ULONG transferred = 0;
	NTSTATUS err = do_copy_payload(dst, r, &transferred);

	if (!err) {
		NT_ASSERT(transferred == expected);
		TRANSFERRED(irp) += transferred;
	}

	return err;
}

/*
 * PAGED_CODE() fails.
 */
static NTSTATUS copy_transfer_buffer(IRP *irp, const URB *urb, vpdo_dev_t *vpdo)
{
	const struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used
	NT_ASSERT(r->TransferBufferLength);

	ULONG buf_sz = get_irp_buffer_size(irp);
	ULONG transferred = (ULONG)TRANSFERRED(irp);

	NT_ASSERT(buf_sz >= transferred);

	if (buf_sz - transferred >= r->TransferBufferLength) {
		auto buf = (char*)get_irp_buffer(irp);
		return do_copy_transfer_buffer(buf + transferred, urb, irp);
	}

	vpdo->len_sent_partial = transferred;
	return STATUS_SUCCESS;
}

/*
* Copy usbip payload to read buffer, usbip_header was handled by previous IRP.
* Userspace app reads usbip header (previous IRP), calculates usbip payload size, reads usbip payload (this IRP).
*/
static PAGEABLE NTSTATUS transfer_partial(IRP *irp, URB *urb)
{
	PAGED_CODE();

	const struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer; // any struct with Transfer* members can be used
	void *dst = try_get_irp_buffer(irp, r->TransferBufferLength);

	return dst ? do_copy_transfer_buffer(dst, urb, irp) : STATUS_BUFFER_TOO_SMALL;
}

static PAGEABLE NTSTATUS urb_isoch_transfer_partial(IRP *irp, URB *urb)
{
	PAGED_CODE();

	const struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;

	ULONG sz = get_payload_size(r);
	void *dst = try_get_irp_buffer(irp, sz);

	return dst ? copy_payload(dst, irp, r, sz) : STATUS_BUFFER_TOO_SMALL;
}

/*
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd.
 */
static PAGEABLE NTSTATUS usb_reset_port(IRP *irp, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

/*
 * vhci_ioctl -> vhci_ioctl_vhub -> get_descriptor_from_nodeconn -> vpdo_get_dsc_from_nodeconn -> req_fetch_dsc -> submit_urbr -> vhci_read
 */
static PAGEABLE NTSTATUS get_descriptor_from_node_connection(IRP *irp, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto r = (const USB_DESCRIPTOR_REQUEST*)get_irp_buffer(urbr->irp);

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // length of r->Data[]

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, data_sz);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
	pkt->wValue.W = r->SetupPacket.wValue;
	pkt->wIndex.W = r->SetupPacket.wIndex;
	pkt->wLength = r->SetupPacket.wLength;

	char buf[USB_SETUP_PKT_STR_BUFBZ];
	TraceURB("ConnectionIndex %lu, %s", r->ConnectionIndex, usb_setup_pkt_str(buf, sizeof(buf), &r->SetupPacket));

	TRANSFERRED(irp) = sizeof(*hdr);
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
static PAGEABLE NTSTATUS sync_reset_pipe_and_clear_stall(IRP *irp, URB *urb, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_control_descriptor_request(IRP *irp, URB *urb, struct urb_req *urbr, bool dir_in, UCHAR recipient)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | 
				(dir_in ? USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, r->TransferBufferLength);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = UCHAR((dir_in ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_STANDARD | recipient);
	pkt->bRequest = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
	pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r->DescriptorType, r->Index);
	pkt->wIndex.W = r->LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
	pkt->wLength = (USHORT)r->TransferBufferLength;

	TRANSFERRED(irp) = sizeof(*hdr);

	if (!dir_in && r->TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, urbr->vpdo);
	}

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_control_get_status_request(IRP *irp, URB *urb, struct urb_req *urbr, UCHAR recipient)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_control_vendor_class_request(IRP *irp, URB *urb, struct urb_req *urbr, UCHAR type, UCHAR recipient)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, 
							EP0, r->TransferFlags | USBD_DEFAULT_PIPE_TRANSFER, r->TransferBufferLength);

	if (err) {
		return err;
	}

	bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = UCHAR((dir_out ? USB_DIR_OUT : USB_DIR_IN) | type | recipient);
	pkt->bRequest = r->Request;
	pkt->wValue.W = r->Value;
	pkt->wIndex.W = r->Index;
	pkt->wLength = (USHORT)r->TransferBufferLength;

	TRANSFERRED(irp) = sizeof(*hdr);

	if (dir_out && r->TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, urbr->vpdo);
	}

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS vendor_device(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS vendor_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS vendor_endpoint(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS vendor_other(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

static PAGEABLE NTSTATUS class_device(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS class_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS class_endpoint(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS class_other(IRP *irp, URB *urb, struct urb_req *urbr)
{
	return urb_control_vendor_class_request(irp, urb, urbr, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

static PAGEABLE NTSTATUS urb_select_configuration(IRP *irp, URB *urb, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	struct _URB_SELECT_CONFIGURATION *r = &urb->UrbSelectConfiguration;
	USB_CONFIGURATION_DESCRIPTOR *cd = r->ConfigurationDescriptor; // nullptr if unconfigured

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	USB_DEFAULT_PIPE_SETUP_PACKET *pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
	pkt->wValue.W = cd ? cd->bConfigurationValue : 0;

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_select_interface(IRP *irp, URB *urb, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return  STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
* The USB bus driver processes this URB at DISPATCH_LEVEL.
*/
static NTSTATUS urb_bulk_or_interrupt_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(r->PipeHandle);

	if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
		Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid,
							r->PipeHandle, r->TransferFlags, r->TransferBufferLength);

	if (err) {
		return err;
	}

	TRANSFERRED(irp) = sizeof(*hdr);

	if (r->TransferBufferLength && is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
		return copy_transfer_buffer(irp, urb, urbr->vpdo);
	}

	return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 * USBD_START_ISO_TRANSFER_ASAP is appended because _URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
static NTSTATUS urb_isoch_transfer(IRP *irp, URB *urb, struct urb_req *urbr)
{
	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	USBD_PIPE_TYPE type = get_endpoint_type(r->PipeHandle);

	if (type != UsbdPipeTypeIsochronous) {
		Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	ULONG sz = get_payload_size(r);

	if (get_irp_buffer_size(irp) - TRANSFERRED(irp) >= sz) {
		return copy_payload(hdr + 1, irp, r, sz);
	}

	urbr->vpdo->len_sent_partial = (ULONG)TRANSFERRED(irp);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS urb_control_transfer_any(IRP *irp, URB *urb, struct urb_req* urbr)
{
	PAGED_CODE();

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;
	static_assert(offsetof(struct _URB_CONTROL_TRANSFER, SetupPacket) == offsetof(struct _URB_CONTROL_TRANSFER_EX, SetupPacket), "assert");

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	NTSTATUS err = set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid,
							r->PipeHandle, r->TransferFlags, r->TransferBufferLength);

	if (err) {
		return err;
	}

	bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

	if (dir_out != is_transfer_dir_out(r)) {
		Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
		return STATUS_INVALID_PARAMETER;
	}

	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(r->SetupPacket), "assert");
	RtlCopyMemory(hdr->u.cmd_submit.setup, r->SetupPacket, sizeof(hdr->u.cmd_submit.setup));

	TRANSFERRED(irp) = sizeof(*hdr);

	if (dir_out && r->TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, urbr->vpdo);
	}

	return STATUS_SUCCESS;
}

/*
 * vhci_internal_ioctl.c handles such functions itself.
 */
static PAGEABLE NTSTATUS urb_function_unexpected(IRP *irp, URB *urb, struct urb_req* urbr)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(irp);
	UNREFERENCED_PARAMETER(urbr);

	USHORT func = urb->UrbHeader.Function;
	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	NT_ASSERT(!TRANSFERRED(irp));
	return STATUS_INTERNAL_ERROR;
}

static PAGEABLE NTSTATUS get_descriptor_from_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_IN), USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS set_descriptor_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_OUT), USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS get_descriptor_from_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_IN), USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS set_descriptor_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_OUT), USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS get_descriptor_from_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_IN), USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS set_descriptor_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_descriptor_request(irp, urb, urbr, bool(USB_DIR_OUT), USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS urb_control_feature_request(IRP *irp, URB *urb, struct urb_req* urbr, UCHAR bRequest, UCHAR recipient)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS set_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS set_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS set_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS set_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

static PAGEABLE NTSTATUS clear_feature_to_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS clear_feature_to_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS clear_feature_to_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS clear_feature_to_other(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_feature_request(irp, urb, urbr, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

static PAGEABLE NTSTATUS get_configuration(IRP *irp, URB *urb, struct urb_req* urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS get_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
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

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS get_status_from_device(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_DEVICE);
}

static PAGEABLE NTSTATUS get_status_from_interface(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_INTERFACE);
}

static PAGEABLE NTSTATUS get_status_from_endpoint(IRP *irp, URB *urb, struct urb_req* urbr)
{
	return urb_control_get_status_request(irp, urb, urbr, USB_RECIP_ENDPOINT);
}

static PAGEABLE NTSTATUS get_status_from_other(IRP *irp, URB *urb, struct urb_req* urbr)
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

	urb_control_transfer_any,
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

	nullptr, // URB_FUNCTION_RESERVED_0X0016

	vendor_device,
	vendor_interface,
	vendor_endpoint,

	class_device,
	class_interface,
	class_endpoint,

	nullptr, // URB_FUNCTION_RESERVE_0X001D

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

	nullptr, // URB_FUNCTION_RESERVE_0X002B
	nullptr, // URB_FUNCTION_RESERVE_0X002C
	nullptr, // URB_FUNCTION_RESERVE_0X002D
	nullptr, // URB_FUNCTION_RESERVE_0X002E
	nullptr, // URB_FUNCTION_RESERVE_0X002F

	urb_function_unexpected, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	urb_function_unexpected, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_control_transfer_any, // URB_FUNCTION_CONTROL_TRANSFER_EX

	nullptr, // URB_FUNCTION_RESERVE_0X0033
	nullptr, // URB_FUNCTION_RESERVE_0X0034

	urb_function_unexpected, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_function_unexpected, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	urb_bulk_or_interrupt_transfer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	urb_isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	nullptr, // 0x0039
	nullptr, // 0x003A
	nullptr, // 0x003B
	nullptr, // 0x003C

	urb_function_unexpected // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

/*
 * PAGED_CODE() fails.
 */
static NTSTATUS usb_submit_urb(IRP *irp, struct urb_req *urbr)
{
	auto urb = (URB*)URB_FROM_IRP(urbr->irp);
	if (!urb) {
		Trace(TRACE_LEVEL_VERBOSE, "Null URB");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	USHORT func = urb->UrbHeader.Function;

	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;
	if (pfunc) {
		return pfunc(irp, urb, urbr);
	}

	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
	return STATUS_INVALID_PARAMETER;
}

static PAGEABLE NTSTATUS store_urbr_partial(IRP *read_irp, struct urb_req *urbr)
{
	PAGED_CODE();

	auto urb = (URB*)URB_FROM_IRP(urbr->irp);
	if (!urb) {
		Trace(TRACE_LEVEL_VERBOSE, "Null URB");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	Trace(TRACE_LEVEL_VERBOSE, "Transfer data");

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ISOCH_TRANSFER:
		st = urb_isoch_transfer_partial(read_irp, urb);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
	case URB_FUNCTION_CONTROL_TRANSFER:
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
	//
	case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:	// _URB_CONTROL_DESCRIPTOR_REQUEST
	case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:	// _URB_CONTROL_DESCRIPTOR_REQUEST
	case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:	// _URB_CONTROL_DESCRIPTOR_REQUEST
	//
	case URB_FUNCTION_CLASS_DEVICE:			// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_CLASS_INTERFACE:		// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_CLASS_ENDPOINT:		// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_CLASS_OTHER:			// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	//
	case URB_FUNCTION_VENDOR_DEVICE:		// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_VENDOR_INTERFACE:		// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_VENDOR_ENDPOINT:		// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
	case URB_FUNCTION_VENDOR_OTHER:			// _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
		st = transfer_partial(read_irp, urb);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "%s: unexpected partial transfer", urb_function_str(urb->UrbHeader.Function));
	}

	return st;
}

static PAGEABLE void debug(IRP *irp)
{
	usbip_header *hdr = (usbip_header*)get_irp_buffer(irp);

	size_t transferred = TRANSFERRED(irp);
	DBG_UNREFERENCED_LOCAL_VARIABLE(transferred);

	size_t pdu_sz = get_pdu_size(hdr);
	NT_ASSERT(transferred == sizeof(*hdr) || (transferred > sizeof(*hdr) && transferred == pdu_sz));

	char buf[DBG_USBIP_HDR_BUFSZ];
	TraceEvents(TRACE_LEVEL_INFORMATION, FLAG_USBIP, 
			"OUT %Iu%s", pdu_sz, dbg_usbip_hdr(buf, sizeof(buf), hdr));
}

/*
* PAGED_CODE() fails.
*/
NTSTATUS cmd_submit(IRP *read_irp, struct urb_req *urbr)
{
	NTSTATUS st = STATUS_INVALID_PARAMETER;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = usb_submit_urb(read_irp, urbr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = usb_reset_port(read_irp, urbr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(read_irp, urbr);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return st;
}

static PAGEABLE NTSTATUS cmd_unlink(IRP *irp, struct urb_req *urbr)
{
	PAGED_CODE();

	usbip_header *hdr = (usbip_header*)try_get_irp_buffer(irp, sizeof(*hdr));
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->vpdo->devid, urbr->seq_num_unlink);

	TRANSFERRED(irp) += sizeof(*hdr);
	return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 */
NTSTATUS store_urbr(IRP *read_irp, struct urb_req *urbr)
{
	Trace(TRACE_LEVEL_VERBOSE, "Transfer header");

	NTSTATUS err = urbr->irp ? cmd_submit(read_irp, urbr) : cmd_unlink(read_irp, urbr);
	if (!err) {
		debug(read_irp);
	}

	return err;
}

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
static void on_pending_irp_read_cancelled(DEVICE_OBJECT *devobj, IRP *irp_read)
{
	UNREFERENCED_PARAMETER(devobj);

	TraceURB("Pending irp read cancelled %p", irp_read);

	IoReleaseCancelSpinLock(irp_read->CancelIrql);

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp_read);
	auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext);

	KIRQL irql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &irql);
	if (vpdo->pending_read_irp == irp_read) {
		vpdo->pending_read_irp = nullptr;
	}
	KeReleaseSpinLock(&vpdo->lock_urbr, irql);

	irp_done(irp_read, STATUS_CANCELLED);
}

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
static NTSTATUS process_read_irp(vpdo_dev_t *vpdo, IRP *read_irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	struct urb_req *urbr = nullptr;
	KIRQL oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->pending_read_irp) {
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (vpdo->urbr_sent_partial) {
		NT_ASSERT(vpdo->len_sent_partial);
		urbr = vpdo->urbr_sent_partial;

		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		status = store_urbr_partial(read_irp, urbr);
		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

		vpdo->len_sent_partial = 0;
	} else {
		NT_ASSERT(!vpdo->len_sent_partial);

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

		IRP *irp = urbr->irp;
		free_urbr(urbr); // FAIL

		if (irp) {
			// urbr irp has cancel routine, if the IoSetCancelRoutine returns nullptr that means IRP was cancelled
			IoAcquireCancelSpinLock(&oldirql);
			bool valid = IoSetCancelRoutine(irp, nullptr);
			IoReleaseCancelSpinLock(oldirql);
			if (valid) {
				TRANSFERRED(irp) = 0;
				irp_done(irp, STATUS_INVALID_PARAMETER);
			}
		}
	} else {
		if (!vpdo->len_sent_partial) {
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
			vpdo->urbr_sent_partial = nullptr;
		}
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
	}

	return status;
}

/*
* ReadFile -> IRP_MJ_READ -> vhci_read
*/
extern "C" PAGEABLE NTSTATUS vhci_read(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(!TRANSFERRED(irp));

	Trace(TRACE_LEVEL_VERBOSE, "Enter irql %!irql!, read buffer %lu", KeGetCurrentIrql(), get_irp_buffer_size(irp));

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		Trace(TRACE_LEVEL_ERROR, "read for non-vhci is not allowed");
		return  irp_done(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->DevicePnPState != Deleted) {
		IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
		auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext);
		status = vpdo && vpdo->plugged ? process_read_irp(vpdo, irp) : STATUS_INVALID_DEVICE_REQUEST;
	}

	NT_ASSERT(TRANSFERRED(irp) <= get_irp_buffer_size(irp)); // before irp_done()

	if (status != STATUS_PENDING) {
		irp_done(irp, status);
	}

	Trace(TRACE_LEVEL_VERBOSE, "Leave %!STATUS!, transferred %Iu", status, TRANSFERRED(irp));
	return status;
}
