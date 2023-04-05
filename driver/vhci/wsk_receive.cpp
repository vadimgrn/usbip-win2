/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive.h"
#include <wdm.h>
#include "trace.h"
#include "wsk_receive.tmh"

#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>

#include "dev.h"
#include "urbtransfer.h"
#include "csq.h"
#include "irp.h"
#include "network.h"
#include "wsk_context.h"
#include "vhub.h"
#include "vhci.h"

namespace
{

constexpr auto check(_In_ ULONG TransferBufferLength, _In_ int actual_length)
{
	return  actual_length >= 0 && ULONG(actual_length) <= TransferBufferLength ? 
		STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
auto assign(_Inout_ ULONG &TransferBufferLength, _In_ int actual_length)
{
	auto err = check(TransferBufferLength, actual_length);
	TransferBufferLength = err ? 0 : actual_length;
	return err;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_ret_submit(_In_ const wsk_context &ctx)
{
	auto &hdr = ctx.hdr;
	NT_ASSERT(hdr.base.command == USBIP_RET_SUBMIT);
	return hdr.u.ret_submit;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
auto select_config(_In_ vpdo_dev_t &vpdo, _Inout_ _URB_SELECT_CONFIGURATION *r)
{
	if (vpdo.actconfig) {
		ExFreePoolWithTag(vpdo.actconfig, USBIP_VHCI_POOL_TAG);
		vpdo.actconfig = nullptr;
	}

	auto cd = r->ConfigurationDescriptor;
	if (!cd) {
		Trace(TRACE_LEVEL_INFORMATION, "Going to unconfigured state");
		vpdo.current_intf_num = 0;
		vpdo.current_intf_alt = 0;
		return STATUS_SUCCESS;
	}

	vpdo.actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, cd->wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo.actconfig) {
		RtlCopyMemory(vpdo.actconfig, cd, cd->wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Failed to allocate wTotalLength %d", cd->wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto status = setup_config(r, vpdo.speed);

	if (NT_SUCCESS(status)) {
		r->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)(0x100 | cd->bConfigurationValue);

		char buf[SELECT_CONFIGURATION_STR_BUFSZ];
		Trace(TRACE_LEVEL_INFORMATION, "%s", select_configuration_str(buf, sizeof(buf), r));
	}

	return status;
}

/*
 * EP0 stall is not an error, control endpoint cannot stall.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_select_configuration(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto err = urb.UrbHeader.Status;

	if (err == EndpointStalled) {
		auto &ret = get_ret_submit(ctx);
		Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d", get_usbd_status(err), ret.status);
		err = USBD_STATUS_SUCCESS;
	}

	return err ? STATUS_UNSUCCESSFUL : select_config(*ctx.vpdo, &urb.UrbSelectConfiguration);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
auto select_interface(vpdo_dev_t &vpdo, _URB_SELECT_INTERFACE *r)
{
	if (!vpdo.actconfig) {
		Trace(TRACE_LEVEL_ERROR, "Device is unconfigured");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	auto &iface = r->Interface;
	auto status = setup_intf(&iface, vpdo.speed, vpdo.actconfig);

	if (NT_SUCCESS(status)) {
		char buf[SELECT_INTERFACE_STR_BUFSZ];
		Trace(TRACE_LEVEL_INFORMATION, "%s", select_interface_str(buf, sizeof(buf), r));

		vpdo.current_intf_num = iface.InterfaceNumber;
		vpdo.current_intf_alt = iface.AlternateSetting;
	}

	return status;
}

/*
 * usb_set_interface can return -EPIPE, especially if a device's interface has only one altsetting.
 *
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * See: drivers/usb/core/message.c, usb_set_interface, usb_clear_halt.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_select_interface(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto &vpdo = *ctx.vpdo;
	auto err = urb.UrbHeader.Status;

	if (err == EndpointStalled) {
		auto &ret = get_ret_submit(ctx);
		auto ifnum = urb.UrbSelectInterface.Interface.InterfaceNumber;

		Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d, InterfaceNumber %d, num_altsetting %d",
			get_usbd_status(err), ret.status, ifnum, get_intf_num_altsetting(vpdo.actconfig, ifnum));

		err = USBD_STATUS_SUCCESS;
	}

	return err ? STATUS_UNSUCCESSFUL : select_interface(vpdo, &urb.UrbSelectInterface);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void cache_string_descriptor(
	_Inout_ vpdo_dev_t& vpdo, _In_ UCHAR index, _In_ USHORT lang_id, _In_ const USB_STRING_DESCRIPTOR &src)
{
	if (src.bLength == sizeof(USB_COMMON_DESCRIPTOR)) {
		TraceDbg("Skip empty string, index %d", index);
		return;
	}

	if (index >= ARRAYSIZE(vpdo.strings)) {
		TraceMsg("Can't save index %d in strings[%d]", index, ARRAYSIZE(vpdo.strings));
		return;
	}

	auto &dest = vpdo.strings[index];
	if (dest) {
		USHORT sz = src.bLength - offsetof(USB_STRING_DESCRIPTOR, bString);
		UNICODE_STRING str{ sz, sz, const_cast<WCHAR*>(src.bString) };
		if (index) {
			TraceDbg("strings[%d] -> '%!WSTR!', ignoring '%!USTR!'", index, dest->bString, &str);
		} else {
			TraceDbg("Ignoring list of supported languages");
		}
		return;
	}
	
	auto sz = src.bLength + sizeof(*src.bString); // + L'\0'

	auto sd = (USB_STRING_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, sz, USBIP_VHCI_POOL_TAG);
	if (!sd) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
		return;
	}

	RtlCopyMemory(sd, &src, src.bLength);
	terminate_by_zero(*sd);
	dest = sd;

	if (index) {
		TraceMsg("Index %d, LangId %#x, '%!WSTR!'", index, lang_id, dest->bString);
	} else {
		TraceMsg("List of supported languages%!BIN!", WppBinary(dest, dest->bLength));
	}
}

/*
 * A request can read descriptor header or full descriptor to obtain its real size.
 * F.e. configuration descriptor is 9 bytes, but the full size is stored in wTotalLength.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_control_descriptor_request(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto &r = urb.UrbControlDescriptorRequest;
	const USB_COMMON_DESCRIPTOR *dsc{};

	if (r.TransferBufferLength < sizeof(*dsc)) {
		Trace(TRACE_LEVEL_WARNING, "Descriptor header expected, TransferBufferLength %lu", r.TransferBufferLength);
		return STATUS_SUCCESS;
	}

	dsc = static_cast<USB_COMMON_DESCRIPTOR*>(ctx.mdl_buf.sysaddr());
	if (!dsc) {
		Trace(TRACE_LEVEL_WARNING, "MmGetSystemAddressForMdlSafe error, can't cache descriptor");
		return STATUS_SUCCESS;
	}

	TraceUrb("%s: bLength %d, %!usb_descriptor_type!, Index %d, LangId %#x %!BIN!",
		  urb_function_str(r.Hdr.Function), dsc->bLength, dsc->bDescriptorType, r.Index, r.LanguageId,
		  WppBinary(dsc, USHORT(r.TransferBufferLength)));

	auto &vpdo = *ctx.vpdo;

	switch (r.DescriptorType) {
	case USB_STRING_DESCRIPTOR_TYPE:
		if (dsc->bDescriptorType == USB_STRING_DESCRIPTOR_TYPE && dsc->bLength == r.TransferBufferLength) {
			auto &sd = *reinterpret_cast<const USB_STRING_DESCRIPTOR*>(dsc);
			auto &osd = *reinterpret_cast<const USB_OS_STRING_DESCRIPTOR*>(dsc);
			if (is_valid(osd)) {
				TraceMsg("MS_VendorCode %#x", osd.MS_VendorCode);
				vpdo.MS_VendorCode = osd.MS_VendorCode;
			} else if (is_valid(sd)) {
				cache_string_descriptor(vpdo, r.Index, r.LanguageId, sd);
			}
		}
		break;
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (!(r.TransferBufferLength == sizeof(vpdo.descriptor) && RtlEqualMemory(dsc, &vpdo.descriptor, sizeof(vpdo.descriptor)))) {
			Trace(TRACE_LEVEL_ERROR, "Device descriptor is not the same");
			vhub_unplug_vpdo(&vpdo);
		}
		break;
	}

	return STATUS_SUCCESS;
}

/*
 * Buffer from the server has no gaps (compacted), SUM(src->actual_length) == actual_length,
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
_IRQL_requires_max_(DISPATCH_LEVEL)
auto fill_isoc_data(_Inout_ _URB_ISOCH_TRANSFER &r, _Inout_ char *buffer, _In_ ULONG length, 
	            _In_ const usbip_iso_packet_descriptor *sd)
{
	auto dir_out = !buffer;
	auto sd_offset = length;

	auto dd = r.IsoPacket + r.NumberOfPackets - 1;
	sd += r.NumberOfPackets - 1;

	for (auto i = r.NumberOfPackets; i; --i, --sd, --dd) { // set dd.Status and dd.Length

		dd->Status = sd->status ? to_windows_status_isoch(sd->status) : USBD_STATUS_SUCCESS;

		if (dir_out) {
			continue; // dd->Length is not used for OUT transfers
		}

		if (!sd->actual_length) {
			dd->Length = 0;
			continue;
		}

		if (sd->actual_length > sd->length) {
			Trace(TRACE_LEVEL_ERROR, "actual_length(%u) > length(%u)", sd->actual_length, sd->length);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd->offset != dd->Offset) { // buffer is compacted, but offsets are intact
			Trace(TRACE_LEVEL_ERROR, "src.offset(%u) != dst.Offset(%lu)", sd->offset, dd->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset >= sd->actual_length) {
			sd_offset -= sd->actual_length;
		} else {
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) >= actual_length(%u)", sd_offset, sd->actual_length);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset > dd->Offset) {// source buffer has no gaps
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) > dst.Offset(%lu)", sd_offset, dd->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset + sd->actual_length > length) {
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) + actual_length(%u) > length(%lu)", 
				sd_offset, sd->actual_length, length);
			return STATUS_INVALID_PARAMETER;
		}

		if (dd->Offset + sd->actual_length > r.TransferBufferLength) {
			Trace(TRACE_LEVEL_ERROR, "dst.Offset(%lu) + src.actual_length(%u) > r.TransferBufferLength(%lu)",
				dd->Offset, sd->actual_length, r.TransferBufferLength);
			return STATUS_INVALID_PARAMETER;
		}

		if (dd->Offset > sd_offset) {
			RtlMoveMemory(buffer + dd->Offset, buffer + sd_offset, sd->actual_length);
		} else { // buffer is filled without gaps from the beginning
			NT_ASSERT(dd->Offset == sd_offset);
		}

		dd->Length = sd->actual_length;
	}

	if (!dir_out && sd_offset) {
		Trace(TRACE_LEVEL_ERROR, "SUM(actual_length) != actual_length(%lu), delta is %lu", length, sd_offset);
		return STATUS_INVALID_PARAMETER; 
	}

	return STATUS_SUCCESS;
}

/*
 * ctx.mdl_buf can't be used, it describes actual_length instead of TransferBufferLength.
 * Try TransferBufferMDL first because it is locked-down and to obey URB_FUNCTION_XXX_USING_CHAINED_MDL.
 * 
 * If BSOD happen, this call should be used
 * make_transfer_buffer_mdl(ctx.mdl_buf, usbip::URB_BUF_LEN, false, IoModifyAccess, urb)
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_transfer_buffer(_In_ void *TransferBuffer, _In_ MDL *TransferBufferMDL)
{
	if (auto buf = TransferBufferMDL) {
		return MmGetSystemAddressForMdlSafe(buf, LowPagePriority | MdlMappingNoExecute);
	}

	NT_ASSERT(TransferBuffer); // make_transfer_buffer_mdl checks it before payload receive
	return TransferBuffer;
}

/*
 * Layout: transfer buffer(IN only), usbip_iso_packet_descriptor[].
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_isoch_transfer(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto &ret = get_ret_submit(ctx);
	auto cnt = ret.number_of_packets;

	auto &r = urb.UrbIsochronousTransfer;
	r.ErrorCount = ret.error_count;

	if (cnt && cnt == ret.error_count) {
		r.Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r.TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r.StartFrame = ret.start_frame;
	}

	ctx.vpdo->current_frame_number = ret.start_frame;

	if (cnt >= 0 && ULONG(cnt) == r.NumberOfPackets) {
		NT_ASSERT(r.NumberOfPackets == number_of_packets(ctx));
		byteswap(ctx.isoc, cnt);
	} else {
		Trace(TRACE_LEVEL_ERROR, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r.NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}

	char *buf{};

	if (is_transfer_direction_in(ctx.hdr)) { // TransferFlags can have wrong direction
		buf = (char*)get_transfer_buffer(r.TransferBuffer, r.TransferBufferMDL);
		if (!buf) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	
	return fill_isoc_data(r, buf, ret.actual_length, ctx.isoc);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_with_transfer_buffer(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto &ret = get_ret_submit(ctx);
	auto &tr = AsUrbTransfer(urb);

	return  tr.TransferBufferLength == ULONG(ret.actual_length) ? STATUS_SUCCESS : // set by prepare_wsk_mdl
		assign(tr.TransferBufferLength, ret.actual_length); // DIR_OUT or !actual_length
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_function_success(_In_ wsk_context&, _Inout_ URB&)
{
	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_function_unexpected(_In_ wsk_context&, _Inout_ URB &urb)
{
	auto func = urb.UrbHeader.Function;
	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) must never be called", urb_function_str(func), func);

	return STATUS_INTERNAL_ERROR;
}

using urb_function_t = NTSTATUS(_In_ wsk_context&, _Inout_ URB&);

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

	urb_with_transfer_buffer, // URB_FUNCTION_CONTROL_TRANSFER
	urb_with_transfer_buffer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER
	urb_isoch_transfer,

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_with_transfer_buffer, // URB_FUNCTION_GET_STATUS_FROM_DEVICE
	urb_with_transfer_buffer, // URB_FUNCTION_GET_STATUS_FROM_INTERFACE
	urb_with_transfer_buffer, // URB_FUNCTION_GET_STATUS_FROM_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVED_0X0016

	urb_with_transfer_buffer, // URB_FUNCTION_VENDOR_DEVICE
	urb_with_transfer_buffer, // URB_FUNCTION_VENDOR_INTERFACE
	urb_with_transfer_buffer, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_with_transfer_buffer, // URB_FUNCTION_CLASS_DEVICE
	urb_with_transfer_buffer, // URB_FUNCTION_CLASS_INTERFACE
	urb_with_transfer_buffer, // URB_FUNCTION_CLASS_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVE_0X001D

	urb_function_success, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL, urb_pipe_request

	urb_with_transfer_buffer, // URB_FUNCTION_CLASS_OTHER
	urb_with_transfer_buffer, // URB_FUNCTION_VENDOR_OTHER

	urb_with_transfer_buffer, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_OTHER, urb_control_feature_request

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	urb_with_transfer_buffer, // URB_FUNCTION_GET_CONFIGURATION
	urb_with_transfer_buffer, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

	urb_with_transfer_buffer, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	nullptr, // URB_FUNCTION_RESERVE_0X002B
	nullptr, // URB_FUNCTION_RESERVE_0X002C
	nullptr, // URB_FUNCTION_RESERVE_0X002D
	nullptr, // URB_FUNCTION_RESERVE_0X002E
	nullptr, // URB_FUNCTION_RESERVE_0X002F

	urb_function_unexpected, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	urb_function_unexpected, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_with_transfer_buffer, // URB_FUNCTION_CONTROL_TRANSFER_EX

	nullptr, // URB_FUNCTION_RESERVE_0X0033
	nullptr, // URB_FUNCTION_RESERVE_0X0034

	urb_function_unexpected, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_function_unexpected, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	urb_with_transfer_buffer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	urb_isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	nullptr, // 0x0039
	nullptr, // 0x003A
	nullptr, // 0x003B
	nullptr, // 0x003C

	urb_function_unexpected // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

/*
if (urb.UrbHeader.Status == EndpointStalled && ctx.irp) {
	if (auto handle = get_pipe_handle(ctx.irp)) { // except default control pipe
		if (auto err = clear_endpoint_stall(*ctx.vpdo, handle, nullptr)) {
			Trace(TRACE_LEVEL_ERROR, "clear_endpoint_stall %!STATUS!", err);
		}
	}
}
*/
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usb_submit_urb(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	{
		auto &ret = get_ret_submit(ctx);
		urb.UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;
	}

        auto func = urb.UrbHeader.Function;
        auto pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;

        auto err = pfunc ? pfunc(ctx, urb) : STATUS_INVALID_PARAMETER;

        if (err && !urb.UrbHeader.Status) { // it's OK if (urb->UrbHeader.Status && !err)
                urb.UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
                TraceDbg("Set USBD_STATUS=%s because return is %!STATUS!", get_usbd_status(urb.UrbHeader.Status), err);
        }

        return err;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usb_reset_port(const usbip_header_ret_submit &ret)
{
	auto err = ret.status;
        auto win_err = to_windows_status(err);

        if (win_err == EndpointStalled) { // control pipe stall is not an error, see urb_select_interface
                Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d", get_usbd_status(win_err), err);
		err = 0;
        }

        return err ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

/*
 * @see internal_ioctl.cpp, send_complete 
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void complete(_Inout_ IRP* &irp, _In_ NTSTATUS status)
{
	NT_ASSERT(irp);
	auto &st = irp->IoStatus;

	st.Status = status;
	if (status == STATUS_CANCELLED) { // see complete_as_canceled
		st.Information = 0;	
	}

	auto old_status = atomic_set_status(irp, ST_RECV_COMPLETE);
	NT_ASSERT(old_status != ST_IRP_CANCELED);

	if (old_status == ST_SEND_COMPLETE) {
		TraceDbg("irp %04x, %!STATUS!, Information %#Ix", ptr4log(irp), st.Status, st.Information);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	irp = nullptr;
}

enum { RECV_NEXT_USBIP_HDR = STATUS_SUCCESS, RECV_MORE_DATA_REQUIRED = STATUS_PENDING };

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(vpdo_dev_t::received_fn)
NTSTATUS ret_submit(_Inout_ wsk_context &ctx)
{
	auto &irp = ctx.irp;
	NT_ASSERT(irp);

	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto st = STATUS_INVALID_PARAMETER;

        switch (auto ioctl = stack->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = usb_submit_urb(ctx, *static_cast<URB*>(URB_FROM_IRP(irp)));
                break;
        case IOCTL_INTERNAL_USB_RESET_PORT:
		st = usb_reset_port(get_ret_submit(ctx));
                break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unexpected IoControlCode %s(%#08lX)", internal_device_control_name(ioctl), ioctl);
	}

	complete(irp, st);
	return RECV_NEXT_USBIP_HDR;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
auto make_mdl_chain(_In_ wsk_context &ctx)
{
	MDL *head{};

	if (!ctx.is_isoc) { // IN
		head = ctx.mdl_buf.get();
		NT_ASSERT(!head->Next);
	} else if (auto &chain = ctx.mdl_buf) { // isoch IN
		auto t = tail(chain);
		t->Next = ctx.mdl_isoc.get();
		head = chain.get();
	} else { // isoch OUT or IN with zero actual_length
		head = ctx.mdl_isoc.get();
	}

	return head;
}

/*
 * If response from a server has data (actual_length > 0), URB function MUST copy it to URB
 * even if UrbHeader.Status != USBD_STATUS_SUCCESS.
 * 
 * Ensure that URB has TransferBuffer and its size is sufficient.
 * Do others checks when payload will be read.
 * 
 * recv_payload -> prepare_wsk_mdl, there is payload to receive.
 * Payload layout:
 * a) DIR_IN: any type of transfer, [transfer_buffer] OR|AND [usbip_iso_packet_descriptor...]
 * b) DIR_OUT: ISOCH, <usbip_iso_packet_descriptor...>
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
auto prepare_wsk_mdl(_Out_ MDL* &mdl, _Inout_ wsk_context &ctx, _Inout_ URB &urb)
{
	if (!has_transfer_buffer(urb)) {
		auto fn = urb.UrbHeader.Function;
		Trace(TRACE_LEVEL_ERROR, "%s(%#x) does not have TransferBuffer", urb_function_str(fn), fn);
		return STATUS_INVALID_PARAMETER;
	}

	auto &tr = AsUrbTransfer(urb);
	auto &ret = get_ret_submit(ctx);

	if (auto err = prepare_isoc(ctx, ret.number_of_packets)) { // sets ctx.is_isoc
		return err;
	}

	auto dir_out = is_transfer_direction_out(ctx.hdr); // TransferFlags can have wrong direction
	bool fail{};

	if (ctx.is_isoc) { // always has payload
		fail = check(tr.TransferBufferLength, ret.actual_length); // do not change buffer length
	} else { // actual_length MUST be assigned, must not have payload for OUT
		fail = assign(tr.TransferBufferLength, ret.actual_length) || dir_out; 
	}

	if (fail || !tr.TransferBufferLength) {
		Trace(TRACE_LEVEL_ERROR, "TransferBufferLength(%lu), actual_length(%d), %!usbip_dir!", 
			tr.TransferBufferLength, ret.actual_length, ctx.hdr.base.direction);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (dir_out) {
		NT_ASSERT(ctx.is_isoc);
		NT_ASSERT(!ctx.mdl_buf);
	} else if (auto err = usbip::make_transfer_buffer_mdl(ctx.mdl_buf, ret.actual_length, ctx.is_isoc, IoWriteAccess, urb)) {
		Trace(TRACE_LEVEL_ERROR, "make_transfer_buffer_mdl %!STATUS!", err);
		return err;
	}

	mdl = make_mdl_chain(ctx);
	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
URB *get_urb(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto ioctl = stack->Parameters.DeviceIoControl.IoControlCode;

	if (ioctl == IOCTL_INTERNAL_USB_SUBMIT_URB) {
		return static_cast<URB*>(URB_FROM_IRP(irp));
	}

	Trace(TRACE_LEVEL_ERROR, "IOCTL_INTERNAL_USB_SUBMIT_URB expected, got %s(%#x)", internal_device_control_name(ioctl), ioctl);
	return nullptr;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto alloc_drain_buffer(_Inout_ wsk_context &ctx, _In_ size_t length)
{  
	auto &irp = ctx.irp;
	NT_ASSERT(!irp);
	return irp = (IRP*)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, length, USBIP_VHCI_POOL_TAG); 
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(vpdo_dev_t::received_fn)
NTSTATUS free_drain_buffer(_Inout_ wsk_context &ctx)
{  
	auto &irp = ctx.irp;
	NT_ASSERT(irp);

	ExFreePoolWithTag(irp, USBIP_VHCI_POOL_TAG);
	irp = nullptr;

	return RECV_NEXT_USBIP_HDR;
};

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_receive(_In_ DEVICE_OBJECT*, _In_ IRP *wsk_irp, _In_reads_opt_(_Inexpressible_("varies")) void *Context)
{
	auto &ctx = *static_cast<wsk_context*>(Context);
	auto vpdo = ctx.vpdo;

	auto &st = wsk_irp->IoStatus;
	TraceWSK("wsk irp %04x, %!STATUS!, Information %Iu", ptr4log(wsk_irp), st.Status, st.Information);

	auto ok = NT_SUCCESS(st.Status);

	auto err = ok && st.Information == vpdo->receive_size ? vpdo->received(ctx) :
		   ok ? STATUS_RECEIVE_PARTIAL : 
		   st.Status; // has nonzero severity code

	switch (err) {
	case RECV_NEXT_USBIP_HDR:
		if (!vpdo->unplugged) {
			sched_receive_usbip_header(&ctx);
		}
		[[fallthrough]];
	case RECV_MORE_DATA_REQUIRED:
		return StopCompletion;
	}
	
	if (vpdo->received == free_drain_buffer) { // ctx.irp is a drain buffer
		free_drain_buffer(ctx);
	} else if (auto &irp = ctx.irp) {
		NT_ASSERT(vpdo->received != ret_submit); // never fails
		complete(irp, STATUS_CANCELLED);
	}
	NT_ASSERT(!ctx.irp);

	TraceMsg("vpdo %04x: unplugging after %!STATUS!", ptr4log(vpdo), NT_SUCCESS(st.Status) ? err : st.Status);
	vhub_unplug_vpdo(vpdo);

	free(&ctx, true);
	return StopCompletion;
}

/*
 * @param received will be called if requested number of bytes are received without error
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void receive(_In_ WSK_BUF &buf, _In_ vpdo_dev_t::received_fn received, _In_ wsk_context &ctx)
{
	NT_ASSERT(usbip::verify(buf, ctx.is_isoc));
	auto &vpdo = *ctx.vpdo;

	vpdo.receive_size = buf.Length; // checked by verify()

	NT_ASSERT(received);
	vpdo.received = received;

	reuse(ctx);

	auto wsk_irp = ctx.wsk_irp; // do not access ctx or wsk_irp after send
	IoSetCompletionRoutine(wsk_irp, on_receive, &ctx, true, true, true);

	auto err = receive(ctx.vpdo->sock, &buf, WSK_FLAG_WAITALL, wsk_irp);
	NT_ASSERT(err != STATUS_NOT_SUPPORTED);

	TraceWSK("wsk irp %04x, %!STATUS!", ptr4log(wsk_irp), err);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(vpdo_dev_t::received_fn)
NTSTATUS drain_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	if (ULONG(length) != length) {
		Trace(TRACE_LEVEL_ERROR, "Buffer size truncation: ULONG(%lu) != size_t(%Iu)", ULONG(length), length);
		return STATUS_INVALID_PARAMETER;
	}

	if (auto addr = alloc_drain_buffer(ctx, length)) {
		ctx.mdl_buf = usbip::Mdl(addr, ULONG(length));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", length);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	if (auto err = ctx.mdl_buf.prepare_nonpaged()) {
		NT_ASSERT(err != RECV_MORE_DATA_REQUIRED);
		Trace(TRACE_LEVEL_ERROR, "prepare_nonpaged %!STATUS!", err);
		free_drain_buffer(ctx);
		return err;
	}

	WSK_BUF buf{ ctx.mdl_buf.get(), 0, length };
	receive(buf, free_drain_buffer, ctx);

	return RECV_MORE_DATA_REQUIRED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(vpdo_dev_t::received_fn)
NTSTATUS recv_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	auto urb = get_urb(ctx.irp);
	if (!urb) {
		return STATUS_INVALID_PARAMETER;
	}

	WSK_BUF buf{ .Length = length };

	if (auto err = prepare_wsk_mdl(buf.Mdl, ctx, *urb)) {
		NT_ASSERT(err != RECV_MORE_DATA_REQUIRED);
		Trace(TRACE_LEVEL_ERROR, "prepare_wsk_mdl %!STATUS!", err);
		return err;
	}

	receive(buf, ret_submit, ctx);
	return RECV_MORE_DATA_REQUIRED;
}

/*
 * For RET_UNLINK irp was completed right after CMD_UNLINK was issued.
 * @see send_cmd_unlink
 *
 * USBIP_RET_UNLINK
 * 1) if UNLINK is successful, status is -ECONNRESET
 * 2) if USBIP_CMD_UNLINK is after USBIP_RET_SUBMIT status is 0
 * See: <kernel>/Documentation/usb/usbip_protocol.rst
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(vpdo_dev_t::received_fn)
NTSTATUS ret_command(_Inout_ wsk_context &ctx)
{
	auto &hdr = ctx.hdr; // IRP must be completed
	ctx.irp = hdr.base.command == USBIP_RET_SUBMIT ? dequeue_irp(*ctx.vpdo, hdr.base.seqnum) : nullptr;

	{
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x <- %Iu%s",
			    ptr4log(ctx.irp), get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, false));
	}

	if (auto sz = get_payload_size(hdr)) {
		auto f = ctx.irp ? recv_payload : drain_payload;
		return f(ctx, sz);
	}

	if (ctx.irp) {
		ret_submit(ctx);
	}

	return RECV_NEXT_USBIP_HDR;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
auto validate_header(_Inout_ usbip_header &hdr)
{
	byteswap_header(hdr, swap_dir::net2host);

	auto &base = hdr.base;
	auto cmd = static_cast<usbip_request_type>(base.command);

	switch (cmd) {
	case USBIP_RET_SUBMIT: {
		auto &ret = hdr.u.ret_submit;
		if (ret.number_of_packets == number_of_packets_non_isoch) {
			ret.number_of_packets = 0;
		} else if (!is_valid_number_of_packets(ret.number_of_packets)) {
			return false;
		}
	}	break;
	case USBIP_RET_UNLINK:
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "USBIP_RET_* expected, got %!usbip_request_type!", cmd);
		return false;
	}

	auto ok = is_valid_seqnum(base.seqnum);

	if (ok) {
		base.direction = extract_dir(base.seqnum); // always zero in server response
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid seqnum %u", base.seqnum);
	}

	return ok;
}

_Function_class_(IO_WORKITEM_ROUTINE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
void receive_usbip_header(_In_ DEVICE_OBJECT*, _In_opt_ void *Context)
{
	auto &ctx = *static_cast<wsk_context*>(Context);

	NT_ASSERT(!ctx.irp); // must be completed and zeroed on every cycle
	ctx.mdl_buf.reset();

	ctx.mdl_hdr.next(nullptr);
	WSK_BUF buf{ ctx.mdl_hdr.get(), 0, sizeof(ctx.hdr) };

	auto received = [] (auto &ctx)
	{
		return validate_header(ctx.hdr) ? ret_command(ctx) : STATUS_INVALID_PARAMETER;
	};

	receive(buf, received, ctx);
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags)
{
	auto vpdo = static_cast<vpdo_dev_t*>(SocketContext);
	TraceMsg("vpdo %04x, Flags %#x", ptr4log(vpdo), Flags);

	vhub_unplug_vpdo(vpdo);
	return STATUS_SUCCESS;
}

/*
 * A WSK application should not call new WSK functions in the context of the IoCompletion routine. 
 * Doing so may result in recursive calls and exhaust the kernel mode stack. 
 * When executing at IRQL = DISPATCH_LEVEL, this can also lead to starvation of other threads.
 *
 * For this reason work queue is used here, but reading of payload does not use it and it's OK.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
void sched_receive_usbip_header(_In_ wsk_context *ctx)
{
	auto vpdo = ctx->vpdo;
	NT_ASSERT(vpdo);

	const auto QueueType = static_cast<WORK_QUEUE_TYPE>(CustomPriorityWorkQueue + LOW_REALTIME_PRIORITY);
	IoQueueWorkItem(vpdo->workitem, receive_usbip_header, QueueType, ctx);
}
