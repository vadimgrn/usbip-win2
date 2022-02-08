#include "read.h"
#include "dbgcommon.h"
#include "trace.h"
#include "read.tmh"

#include "irp.h"
#include "proto.h"
#include "usbd_helper.h"
#include "urbtransfer.h"
#include "devconf.h"
#include "pdu.h"
#include "ch9.h"
#include "ch11.h"
#include "dev.h"
#include "usb_util.h"

namespace
{

inline auto& TRANSFERRED(IRP *irp) { return irp->IoStatus.Information; }
inline auto TRANSFERRED(const IRP *irp) { return irp->IoStatus.Information; }

inline auto get_irp_buffer(const IRP *irp)
{
	return irp->AssociatedIrp.SystemBuffer;
}

auto get_irp_buffer_size(const IRP *irp)
{
	auto irpstack = IoGetCurrentIrpStackLocation(const_cast<IRP*>(irp));
	return irpstack->Parameters.Read.Length;
}

auto try_get_irp_buffer(const IRP *irp, size_t min_size)
{
	NT_ASSERT(!TRANSFERRED(irp));

	auto sz = get_irp_buffer_size(irp);
	return sz >= min_size ? get_irp_buffer(irp) : nullptr;
}

inline auto get_usbip_header(const IRP *irp)
{
	auto ptr = try_get_irp_buffer(irp, sizeof(usbip_header));
	return static_cast<usbip_header*>(ptr);
}

const void *get_urb_buffer(void *buf, MDL *bufMDL)
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
NTSTATUS do_copy_payload(void *dst_buf, const _URB_ISOCH_TRANSFER &r, ULONG *transferred)
{
	NT_ASSERT(dst_buf);

	*transferred = 0;
	bool mdl = r.Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;

	auto src_buf = get_urb_buffer(mdl ? nullptr : r.TransferBuffer, r.TransferBufferMDL);
	if (!src_buf) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto buf_sz = is_endpoint_direction_out(r.PipeHandle) ? r.TransferBufferLength : 0; // TransferFlags can have wrong direction

	RtlCopyMemory(dst_buf, src_buf, buf_sz);
	*transferred += buf_sz;

	auto dsc = reinterpret_cast<usbip_iso_packet_descriptor*>((char*)dst_buf + buf_sz);
	ULONG sum = 0;

	for (ULONG i = 0; i < r.NumberOfPackets; ++dsc) {

		auto offset = r.IsoPacket[i].Offset;
		auto next_offset = ++i < r.NumberOfPackets ? r.IsoPacket[i].Offset : r.TransferBufferLength;

		if (next_offset >= offset && next_offset <= r.TransferBufferLength) {
			dsc->offset = offset;
			dsc->length = next_offset - offset;
			dsc->actual_length = 0;
			dsc->status = 0;
			sum += dsc->length;
		} else {
			Trace(TRACE_LEVEL_ERROR, "[%lu] next_offset(%lu) >= offset(%lu) && next_offset <= r.TransferBufferLength(%lu)",
						i, next_offset, offset, r.TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}
	}

	*transferred += r.NumberOfPackets*sizeof(*dsc);

	NT_ASSERT(sum == r.TransferBufferLength);
	return STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
*/
auto get_payload_size(const _URB_ISOCH_TRANSFER &r)
{
	ULONG len = r.NumberOfPackets*sizeof(usbip_iso_packet_descriptor);

	if (is_endpoint_direction_out(r.PipeHandle)) {
		len += r.TransferBufferLength;
	}

	return len;
}

NTSTATUS do_copy_transfer_buffer(void *dst, const URB *urb, IRP *irp)
{
	NT_ASSERT(dst);

	bool mdl = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
	NT_ASSERT(urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

	auto r = AsUrbTransfer(urb);

	auto buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);
	if (buf) {
		RtlCopyMemory(dst, buf, r->TransferBufferLength);
		TRANSFERRED(irp) += r->TransferBufferLength;
	}

	return buf ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/*
* PAGED_CODE() fails.
*/
NTSTATUS copy_payload(void *dst, IRP *irp, const _URB_ISOCH_TRANSFER &r, [[maybe_unused]] ULONG expected)
{
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
NTSTATUS copy_transfer_buffer(IRP *irp, const URB *urb, vpdo_dev_t*)
{
	auto r = AsUrbTransfer(urb);
	NT_ASSERT(r->TransferBufferLength);

	auto buf_sz = get_irp_buffer_size(irp);
	auto transferred = (ULONG)TRANSFERRED(irp);

	NT_ASSERT(buf_sz >= transferred);

	if (buf_sz - transferred >= r->TransferBufferLength) {
		auto buf = (char*)get_irp_buffer(irp);
		return do_copy_transfer_buffer(buf + transferred, urb, irp);
	}

//	vpdo->len_sent_partial = transferred;
	return STATUS_SUCCESS;
}

/*
* Copy usbip payload to read buffer, usbip_header was handled by previous IRP.
* Userspace app reads usbip header (previous IRP), calculates usbip payload size, reads usbip payload (this IRP).
*/
PAGEABLE NTSTATUS transfer_partial(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto r = AsUrbTransfer(urb);
	auto dst = try_get_irp_buffer(irp, r->TransferBufferLength);

	return dst ? do_copy_transfer_buffer(dst, urb, irp) : STATUS_BUFFER_TOO_SMALL;
}

PAGEABLE NTSTATUS urb_isoch_transfer_partial(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto &r = urb->UrbIsochronousTransfer;

	auto sz = get_payload_size(r);
	void *dst = try_get_irp_buffer(irp, sz);

	return dst ? copy_payload(dst, irp, r, sz) : STATUS_BUFFER_TOO_SMALL;
}

/*
* See: <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd.
*/
PAGEABLE NTSTATUS usb_reset_port(IRP *irp)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

//	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, 0);
//	if (err) {
//		return err;
//	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_RT_PORT; // USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER
	pkt->bRequest = USB_REQUEST_SET_FEATURE;
	pkt->wValue.W = USB_PORT_FEAT_RESET;

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

/*
* vhci_ioctl -> vhci_ioctl_vhub -> get_descriptor_from_nodeconn -> vpdo_get_dsc_from_nodeconn -> req_fetch_dsc -> submit_urbr -> vhci_read
*/
PAGEABLE NTSTATUS get_descriptor_from_node_connection(IRP *irp)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto urb_irp = irp;
	auto &r = *(const USB_DESCRIPTOR_REQUEST*)get_irp_buffer(urb_irp);

	auto irpstack = IoGetCurrentIrpStackLocation(urb_irp);
	auto data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // length of r.Data[]

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, data_sz);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
	pkt->wValue.W = r.SetupPacket.wValue;
	pkt->wIndex.W = r.SetupPacket.wIndex;
	pkt->wLength = r.SetupPacket.wLength;

	char buf[USB_SETUP_PKT_STR_BUFBZ];
	TraceUrb("ConnectionIndex %lu, %s", r.ConnectionIndex, usb_setup_pkt_str(buf, sizeof(buf), &r.SetupPacket));

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

/*
* Any URBs queued for such an endpoint should normally be unlinked by the driver before clearing the halt condition,
* as described in sections 5.7.5 and 5.8.5 of the USB 2.0 spec.
*
* Thus, a driver must call URB_FUNCTION_ABORT_PIPE before URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL.
* For that reason vhci_ioctl_abort_pipe(urbr->vpdo, r.PipeHandle) is not called here.
*
* Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt which
* a) Issues USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT # URB_FUNCTION_SYNC_CLEAR_STALL
* b) Calls usb_reset_endpoint # URB_FUNCTION_SYNC_RESET_PIPE
*
* See: <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
<linux>/drivers/usb/core/message.c, usb_clear_halt
*/
PAGEABLE NTSTATUS sync_reset_pipe_and_clear_stall(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbPipeRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT;
	pkt->bRequest = USB_REQUEST_CLEAR_FEATURE;
	pkt->wValue.W = USB_FEATURE_ENDPOINT_STALL; // USB_ENDPOINT_HALT
	pkt->wIndex.W = get_endpoint_address(r.PipeHandle);

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_control_descriptor_request(IRP *irp, URB *urb, bool dir_in, UCHAR recipient)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlDescriptorRequest;

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | 
		(dir_in ? USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, r.TransferBufferLength);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = UCHAR((dir_in ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_STANDARD | recipient);
	pkt->bRequest = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
	pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r.DescriptorType, r.Index);
	pkt->wIndex.W = r.LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
	pkt->wLength = (USHORT)r.TransferBufferLength;

	TRANSFERRED(irp) = sizeof(*hdr);

	if (!dir_in && r.TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, get_vpdo(urb_irp));
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_control_get_status_request(IRP *irp, URB *urb, UCHAR recipient)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlGetStatusRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;
	
	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, r.TransferBufferLength);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | recipient;
	pkt->bRequest = USB_REQUEST_GET_STATUS;
	pkt->wIndex.W = r.Index;
	pkt->wLength = (USHORT)r.TransferBufferLength; // must be 2

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_control_vendor_class_request(IRP *irp, URB *urb, UCHAR type, UCHAR recipient)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlVendorClassRequest;
	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, 
		EP0, r.TransferFlags | USBD_DEFAULT_PIPE_TRANSFER, r.TransferBufferLength);

	if (err) {
		return err;
	}

	bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = UCHAR((dir_out ? USB_DIR_OUT : USB_DIR_IN) | type | recipient);
	pkt->bRequest = r.Request;
	pkt->wValue.W = r.Value;
	pkt->wIndex.W = r.Index;
	pkt->wLength = (USHORT)r.TransferBufferLength;

	TRANSFERRED(irp) = sizeof(*hdr);

	if (dir_out && r.TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, get_vpdo(urb_irp));
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vendor_device(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS vendor_interface(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS vendor_endpoint(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS vendor_other(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS class_device(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS class_interface(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS class_endpoint(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS class_other(IRP *irp, URB *urb)
{
	return urb_control_vendor_class_request(irp, urb, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS urb_select_configuration(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbSelectConfiguration;
	USB_CONFIGURATION_DESCRIPTOR *cd = r.ConfigurationDescriptor; // nullptr if unconfigured

	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
	pkt->wValue.W = cd ? cd->bConfigurationValue : 0;

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_select_interface(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbSelectInterface;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
	pkt->bRequest = USB_REQUEST_SET_INTERFACE;
	pkt->wValue.W = r.Interface.AlternateSetting;
	pkt->wIndex.W = r.Interface.InterfaceNumber;

	TRANSFERRED(irp) = sizeof(*hdr);
	return  STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
* The USB bus driver processes this URB at DISPATCH_LEVEL.
*/
NTSTATUS urb_bulk_or_interrupt_transfer(IRP *irp, URB *urb)
{
	auto &r = urb->UrbBulkOrInterruptTransfer;
	auto type = get_endpoint_type(r.PipeHandle);

	if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
		Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto urb_irp = irp;
	
	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid,
		r.PipeHandle, r.TransferFlags, r.TransferBufferLength);

	if (err) {
		return err;
	}

	TRANSFERRED(irp) = sizeof(*hdr);

	if (r.TransferBufferLength && is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
		return copy_transfer_buffer(irp, urb, get_vpdo(urb_irp));
	}

	return STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
* USBD_START_ISO_TRANSFER_ASAP is appended because _URB_GET_CURRENT_FRAME_NUMBER is not implemented.
*/
NTSTATUS urb_isoch_transfer(IRP *irp, URB *urb)
{
	auto &r = urb->UrbIsochronousTransfer;
	auto type = get_endpoint_type(r.PipeHandle);

	if (type != UsbdPipeTypeIsochronous) {
		Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
		return STATUS_INVALID_PARAMETER;
	}

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto urb_irp = irp;
	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid,
						r.PipeHandle, r.TransferFlags | USBD_START_ISO_TRANSFER_ASAP, r.TransferBufferLength);

	if (err) {
		return err;
	}

	hdr->u.cmd_submit.start_frame = r.StartFrame;
	hdr->u.cmd_submit.number_of_packets = r.NumberOfPackets;

	TRANSFERRED(irp) = sizeof(*hdr);
	auto sz = get_payload_size(r);

	if (get_irp_buffer_size(irp) - TRANSFERRED(irp) >= sz) {
		return copy_payload(hdr + 1, irp, r, sz);
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_control_transfer_any(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto &r = urb->UrbControlTransfer;
	static_assert(offsetof(_URB_CONTROL_TRANSFER, SetupPacket) == offsetof(_URB_CONTROL_TRANSFER_EX, SetupPacket));

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto urb_irp = irp;
	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid,
							r.PipeHandle, r.TransferFlags, r.TransferBufferLength);

	if (err) {
		return err;
	}

	bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

	if (dir_out != is_transfer_dir_out(&r)) {
		Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
		return STATUS_INVALID_PARAMETER;
	}

	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(r.SetupPacket));
	RtlCopyMemory(hdr->u.cmd_submit.setup, r.SetupPacket, sizeof(hdr->u.cmd_submit.setup));

	TRANSFERRED(irp) = sizeof(*hdr);

	if (dir_out && r.TransferBufferLength) {
		return copy_transfer_buffer(irp, urb, get_vpdo(urb_irp));
	}

	return STATUS_SUCCESS;
}

/*
* vhci_internal_ioctl.c handles such functions itself.
*/
PAGEABLE NTSTATUS urb_function_unexpected([[maybe_unused]] IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto func = urb->UrbHeader.Function;
	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	NT_ASSERT(!TRANSFERRED(irp));
	return STATUS_INTERNAL_ERROR;
}

PAGEABLE NTSTATUS get_descriptor_from_device(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb, bool(USB_DIR_IN), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_descriptor_to_device(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb, bool(USB_DIR_OUT), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_descriptor_from_interface(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb, bool(USB_DIR_IN), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_descriptor_to_interface(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb,  bool(USB_DIR_OUT), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_descriptor_from_endpoint(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb, bool(USB_DIR_IN), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_descriptor_to_endpoint(IRP *irp, URB *urb)
{
	return urb_control_descriptor_request(irp, urb, bool(USB_DIR_OUT), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS urb_control_feature_request(IRP *irp, URB *urb, UCHAR bRequest, UCHAR recipient)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlFeatureRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, 0);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | recipient;
	pkt->bRequest = bRequest;
	pkt->wValue.W = r.FeatureSelector;
	pkt->wIndex.W = r.Index;

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS set_feature_to_device(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_feature_to_interface(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_feature_to_endpoint(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_feature_to_other(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb,  USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS clear_feature_to_device(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS clear_feature_to_interface(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS clear_feature_to_endpoint(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS clear_feature_to_other(IRP *irp, URB *urb)
{
	return urb_control_feature_request(irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS get_configuration(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlGetConfigurationRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

	auto urb_irp = irp;

	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, r.TransferBufferLength);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	pkt->bRequest = USB_REQUEST_GET_CONFIGURATION;
	pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_interface(IRP *irp, URB *urb)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &r = urb->UrbControlGetInterfaceRequest;
	const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

	auto urb_irp = irp;
	auto err = set_cmd_submit_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, EP0, TransferFlags, r.TransferBufferLength);
	if (err) {
		return err;
	}

	auto pkt = get_submit_setup(hdr);
	pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
	pkt->bRequest = USB_REQUEST_GET_INTERFACE;
	pkt->wIndex.W = r.Interface;
	pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

	TRANSFERRED(irp) = sizeof(*hdr);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_status_from_device(IRP *irp, URB *urb)
{
	return urb_control_get_status_request(irp, urb,  USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_status_from_interface(IRP *irp, URB *urb)
{
	return urb_control_get_status_request(irp, urb, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_status_from_endpoint(IRP *irp, URB *urb)
{
	return urb_control_get_status_request(irp, urb, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS get_status_from_other(IRP *irp, URB *urb)
{
	return urb_control_get_status_request(irp, urb, USB_RECIP_OTHER);
}

using urb_function_t = NTSTATUS(IRP *irp, URB *urb);

urb_function_t* const urb_functions[] =
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
NTSTATUS usb_submit_urb(IRP *irp)
{
	auto urb_irp = irp;

	auto urb = (URB*)URB_FROM_IRP(urb_irp);
	if (!urb) {
		Trace(TRACE_LEVEL_VERBOSE, "Null URB");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	auto func = urb->UrbHeader.Function;

	auto pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;
	if (pfunc) {
		return pfunc(irp, urb);
	}

	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
	return STATUS_INVALID_PARAMETER;
}

PAGEABLE void debug(IRP *read_irp, IRP *irp)
{
	auto hdr = (usbip_header*)get_irp_buffer(read_irp);
	auto pdu_sz = get_pdu_size(hdr);

	[[maybe_unused]] auto transferred = TRANSFERRED(read_irp);
	NT_ASSERT(transferred == sizeof(*hdr) || (transferred > sizeof(*hdr) && transferred == pdu_sz));

	auto uirp = reinterpret_cast<uintptr_t>(irp);

	char buf[DBG_USBIP_HDR_BUFSZ];
	TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x -> %Iu%s", 
			static_cast<UINT32>(uirp), pdu_sz, dbg_usbip_hdr(buf, sizeof(buf), hdr));
}

/*
* PAGED_CODE() fails.
*/
NTSTATUS cmd_submit(IRP *read_irp)
{
	NTSTATUS st = STATUS_INVALID_PARAMETER;

	//auto irpstack = IoGetCurrentIrpStackLocation(urbr->irp);
	auto ioctl_code = IOCTL_INTERNAL_USB_SUBMIT_URB; // irpstack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = usb_submit_urb(read_irp);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = usb_reset_port(read_irp);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(read_irp);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return st;
}

PAGEABLE NTSTATUS cmd_unlink(IRP *irp)
{
	PAGED_CODE();

	auto hdr = get_usbip_header(irp);
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	//set_cmd_unlink_usbip_header(hdr, get_seqnum(urb_irp), get_vpdo(urb_irp)->devid, get_seqnum(urb_irp)_unlink);

	TRANSFERRED(irp) += sizeof(*hdr);
	return STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
*/
NTSTATUS do_read(vpdo_dev_t *vpdo, IRP *read_irp, IRP *urb_irp)
{
//	TraceCall("irp %04x: Transfer header", static_cast<UINT32>(uirp));

	auto err = urbr->irp ? cmd_submit(read_irp, urbr) : cmd_unlink(read_irp, urbr);
	if (!err) {
		debug(read_irp, urbr->irp);
	}

	return err;
}

PAGEABLE NTSTATUS store_urbr_partial(IRP *read_irp)
{
	PAGED_CODE();

	auto urb_irp = read_irp;

	auto urb = (URB*)URB_FROM_IRP(urb_irp);
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

auto pop_urb_irp(IRP* &urb_irp, vpdo_dev_t *vpdo, IRP *read_irp)
{
	urb_irp = IoCsqRemoveNextIrp(&vpdo->urb_rx_irps_queue, nullptr);
	if (!urb_irp) {
		IoCsqInsertIrp(&vpdo->read_irp_queue, read_irp, nullptr);
		return STATUS_PENDING;
	}

	return STATUS_INVALID_PARAMETER;
}

auto process_read_irp(vpdo_dev_t *vpdo, IRP *read_irp)
{
	IRP *urb_irp{};
	if (auto err = pop_urb_irp(urb_irp, vpdo, read_irp)) {
		return err;
	}

	return STATUS_INVALID_PARAMETER;
}

} // namespace


/*
* ReadFile -> IRP_MJ_READ -> vhci_read
*/
extern "C" PAGEABLE NTSTATUS vhci_read(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(!TRANSFERRED(irp));

	TraceCall("Enter irql %!irql!, read buffer %lu", KeGetCurrentIrql(), get_irp_buffer_size(irp));

	auto vhci = to_vhci_or_null(devobj);
	if (!vhci) {
		Trace(TRACE_LEVEL_ERROR, "read for non-vhci is not allowed");
		return  CompleteRequest(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->PnPState != pnp_state::Removed) {
		auto irpstack = IoGetCurrentIrpStackLocation(irp);
		auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext);
		status = vpdo && !vpdo->unplugged ? process_read_irp(vpdo, irp) : STATUS_INVALID_DEVICE_REQUEST;
	}

	NT_ASSERT(TRANSFERRED(irp) <= get_irp_buffer_size(irp)); // before CompleteRequest()

	if (status != STATUS_PENDING) {
		CompleteRequest(irp, status);
	}

	TraceCall("Leave %!STATUS!, transferred %Iu", status, TRANSFERRED(irp));
	return status;
}
