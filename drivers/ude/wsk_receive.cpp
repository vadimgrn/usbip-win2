/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive.h"
#include "trace.h"
#include "wsk_receive.tmh"

#include "ioctl.h"
#include "context.h"
#include "ring_buffer.h"
#include "wsk_context.h"
#include "urbtransfer.h"
#include "request_list.h"

#include <libdrv/usbd_helper.h>
#include <libdrv/dbgcommon.h>
#include <libdrv/usbdsc.h>
#include <libdrv/pdu.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void log(_In_ const USB_DEVICE_DESCRIPTOR &d)
{
	TraceUrb("DEV: bLength %d, bcdUSB %#x, bDeviceClass %#x, bDeviceSubClass %#x, bDeviceProtocol %#x, "
		"bMaxPacketSize0 %d, idVendor %#x, idProduct %#x, bcdDevice %#x, "
		"iManufacturer %d, iProduct %d, iSerialNumber %d, bNumConfigurations %d",
		d.bLength, d.bcdUSB, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol, 
		d.bMaxPacketSize0, d.idVendor, d.idProduct, d.bcdDevice, 
		d.iManufacturer, d.iProduct, d.iSerialNumber, d.bNumConfigurations);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void log(_In_ const USB_CONFIGURATION_DESCRIPTOR &d)
{
	TraceUrb("CFG: bLength %d, wTotalLength %hu(%#x), bNumInterfaces %d, "
		 "bConfigurationValue %d, iConfiguration %d, bmAttributes %#x, MaxPower %d",
		  d.bLength, d.wTotalLength, d.wTotalLength, d.bNumInterfaces, 
		  d.bConfigurationValue, d.iConfiguration, d.bmAttributes, d.MaxPower);
}

/*
 * For LS/FS interrupt endpoint only.
 * @param bInterval milliseconds
 * @return 1-16, for HS interrupt endpoint
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
UCHAR to_high_speed_interval(_In_ UCHAR bInterval)
{
        enum { MIN_INTVL = 1, MAX_INTVL = 16 }; // result

        NT_ASSERT(bInterval);
        auto microframes = 8*bInterval;
	
        for (UCHAR i = MIN_INTVL; i <= MAX_INTVL; ++i) {
                if ((1 << (i - 1)) >= microframes) {
                        return i;
                }
        }
	
        return MAX_INTVL;
}

/*
 * UDE/USBHUB3 internally treats all devices as High-Speed.
 * Full-Speed bulk endpoints have wMaxPacketSize <= 64, but HS requires 512.
 * USBHUB3 rejects the configuration descriptor if bulk endpoints don't match HS rules,
 * causing repeated enumeration failures ("Invalid Configuration Descriptor").
 *
 * USB_SPEED_FULL audio devices do not work if ISOCH IN/OUT USB_ENDPOINT_DESCRIPTOR.bInterval = 1. 
 * ucx01000!UrbHandler_USBPORTStyle_Legacy_IsochTransfer completes IRP with USBD_STATUS_INVALID_PARAMETER,
 * this error can be observed in the filter driver, this driver will not get ISOCH transfers at all.
 * it always treats bInterval as 0.125ms intervals, and it doesn't care if everything else in the device
 * descriptor or speed is correct.
 *
 * Isochronous transfers can only be used by full-speed and high-speed devices.
 * For devices and host controllers that can operate at full speed, the period is measured in units of 1 millisecond frames.
 * 
 * For devices and host controllers that can operate at high speed, the period is measured in units of microframes.
 * There are eight microframes in each 1 millisecond frame.
 * The period is related to the value in bInterval by the formula 2**(bInterval - 1), the result is number of microframes.
 *
 * 5.5.3 Control Transfer Packet Size Constraints
 * The allowable maximum control transfer data payload sizes for full-speed devices is 8, 16, 32, or 64 bytes;
 * for high-speed devices, it is 64 bytes and for low-speed devices, it is 8 bytes.
 * 
 * 5.6.4 Isochronous Transfer Bus Access Constraints
 * An isochronous endpoint must specify its required bus access period. Full-/high-speed endpoints must specify
 * a desired period as (2**bInterval-1)*F, where bInterval is in the range 1-16 and F is 125µs for high-speed
 * and 1ms for full-speed.
 * 
 * 5.7.4 Interrupt Transfer Packet Size Constraints
 * A full-speed endpoint can specify a desired period from 1 ms to 255 ms. Low-speed endpoints are limited
 * to specifying only 10 ms to 255 ms. High-speed endpoints can specify a desired period (2**bInterval-1)*125µs,
 * where bInterval is in the range 1-16.
 *
 * 5.8.3 Bulk Transfer Packet Size Constraints
 * An endpoint for bulk transfers specifies the maximum data payload size that the endpoint can accept from
 * or transmit to the bus. The USB defines the allowable maximum bulk data payload sizes to be only 8, 16,
 * 32, or 64 bytes for full-speed endpoints and 512 bytes for high-speed endpoints. A low-speed device must
 * not have bulk endpoints
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void patch_config(_In_opt_ USB_CONFIGURATION_DESCRIPTOR *cd)
{
        for (USB_COMMON_DESCRIPTOR *cur{};
             (cur = libdrv::find_next(cd, USB_ENDPOINT_DESCRIPTOR_TYPE, cur)); ) {

                if (cur->bLength < sizeof(USB_ENDPOINT_DESCRIPTOR)) [[unlikely]] {
                        Trace(TRACE_LEVEL_ERROR, "Truncated endpoint descriptor discovered, bLength %d", cur->bLength);
                        break;
                }

                auto &e = *reinterpret_cast<USB_ENDPOINT_DESCRIPTOR*>(cur);

                auto old_pkt = e.wMaxPacketSize; // max payload size for this endpoint
                auto old_intvl = e.bInterval; // polling interval, frames

                switch (usb_endpoint_type(e)) {
                case UsbdPipeTypeBulk:
                        e.wMaxPacketSize = 512; // fixed value for HS
                        break;
                case UsbdPipeTypeIsochronous: // 2**(bInterval - 1) frames
                        enum : UCHAR { MIN_INTVL = 1, MAX_INTVL = 16 };
                        if (!(e.bInterval >= MIN_INTVL && e.bInterval <= MAX_INTVL)) [[unlikely]] {
                                Trace(TRACE_LEVEL_WARNING, "Isochronous interval %d out of spec bounds", e.bInterval);
                                e.bInterval = min(max(e.bInterval, MIN_INTVL), MAX_INTVL);
                        }
                        e.bInterval = min(static_cast<UCHAR>(e.bInterval + 3), MAX_INTVL); // 2**(bInterval-1) microframes
                        break;
                case UsbdPipeTypeInterrupt: // 1-255 ms
                        e.bInterval = to_high_speed_interval(e.bInterval); // 2**(bInterval-1) microframes
                        break;
                case UsbdPipeTypeControl:
                        Trace(TRACE_LEVEL_WARNING, "control endpoint found in configuration descriptor");
                        break;
                }

                if (auto eq = e.wMaxPacketSize == old_pkt && e.bInterval == old_intvl; !eq) {
                        TraceDbg("bLength %d, %!usb_descriptor_type!, bEndpointAddress %#x, "
                                "bmAttributes %#x, wMaxPacketSize %d (was %d), bInterval %d (was %d)", 
                                e.bLength, e.bDescriptorType, e.bEndpointAddress, e.bmAttributes, 
                                e.wMaxPacketSize, old_pkt, e.bInterval, old_intvl);
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto validate(_In_ const iso_packet_descriptor &sd, _In_ ULONG64 dd_offset, _In_ ULONG TransferBufferLength)
{
        if (sd.actual_length > sd.length) [[unlikely]] {
                Trace(TRACE_LEVEL_ERROR, "actual_length %u > length %u", sd.actual_length, sd.length);
                return false;
        }

        if (sd.offset != dd_offset) [[unlikely]] { // buffer is compacted, but offsets are intact
                Trace(TRACE_LEVEL_ERROR, "offset %u != Offset %Iu", sd.offset, dd_offset);
                return false;
        }

        if (dd_offset + sd.actual_length > TransferBufferLength) [[unlikely]] {
                Trace(TRACE_LEVEL_ERROR, "offset %u + actual_length %u > TransferBufferLength %lu",
                                          sd.offset, sd.actual_length, TransferBufferLength);
                return false;
        }

        return true;
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
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto fill_isoc_data(
        _Inout_ _URB_ISOCH_TRANSFER &r, _In_opt_ UCHAR *buffer, _In_ ULONG actual_length, _In_ const iso_packet_descriptor *src)
{
	bool dir_in = buffer;

	for (auto i = static_cast<LONG64>(r.NumberOfPackets) - 1; i >= 0; --i) { // set dd.Status and dd.Length
                auto &sd = src[i];

                auto &dd = r.IsoPacket[i];
                NT_ASSERT(!dd.Length);
                dd.Status = sd.status ? to_windows_status_isoch(sd.status) : USBD_STATUS_SUCCESS;

                if (!(dir_in && sd.actual_length)) { // dd->Length is not used for OUT transfers
                        continue;
                }

                if (!validate(sd, dd.Offset, r.TransferBufferLength)) [[unlikely]] {
                        return STATUS_INVALID_PARAMETER;
                }

                dd.Length = sd.actual_length;

                if (actual_length < sd.actual_length) [[unlikely]] {
                        Trace(TRACE_LEVEL_ERROR, "actual_length %lu < [].actual_length %lu", actual_length, sd.actual_length);
                        return STATUS_INVALID_PARAMETER;
                }

                actual_length -= sd.actual_length;

                if (auto src_offset = actual_length; dd.Offset < src_offset) [[unlikely]] { // buffer has no gaps
                        Trace(TRACE_LEVEL_ERROR, "Offset %lu < src_offset %lu", dd.Offset, src_offset);
                        return STATUS_INVALID_PARAMETER;
                } else if (dd.Offset > src_offset) {
                        RtlMoveMemory(buffer + dd.Offset, buffer + src_offset, sd.actual_length);
                }
        }

        if (dir_in && actual_length) {
                Trace(TRACE_LEVEL_ERROR,"SUM([].actual_length) != actual_length, delta is %Iu", actual_length);
                return STATUS_INVALID_PARAMETER; 
        }

        return STATUS_SUCCESS;
}

/**
 * @see libdrv::get_payload_size
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto fill_isoc_data(
        _Inout_ _URB_ISOCH_TRANSFER &r, _In_opt_ UCHAR *buffer, _In_ size_t actual_length, _In_ ring_buffer_data *data)
{
        bool dir_in = buffer;
        auto iso_len = r.NumberOfPackets*sizeof(iso_packet_descriptor);

        ring_buffer rb(data);
        if (auto payload = (dir_in ? actual_length : 0) + iso_len; rb.size() < payload) {
                Trace(TRACE_LEVEL_ERROR, "buffer size %Iu < %Iu", rb.size(), payload);
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto iso_data = *data; // shares the same buffer with rb
        ring_buffer iso(&iso_data); // usbip_iso_packet_descriptor[]
        if (dir_in) {
                iso.skip(actual_length);
        }

        for (ULONG i = 0; i < r.NumberOfPackets; ++i) { // set dd.Status and dd.Length

                iso_packet_descriptor sd;
                iso.read(&sd, sizeof(sd));
                byteswap(&sd, 1);

                auto &dd = r.IsoPacket[i];
                NT_ASSERT(!dd.Length);
                dd.Status = sd.status ? to_windows_status_isoch(sd.status) : USBD_STATUS_SUCCESS;

                if (!(dir_in && sd.actual_length)) { // dd->Length is not used for OUT transfers
                        continue;
                }

                if (!validate(sd, dd.Offset, r.TransferBufferLength)) [[unlikely]] {
                        return STATUS_INVALID_PARAMETER;
                }

                dd.Length = sd.actual_length;
                rb.read(buffer + dd.Offset, sd.actual_length);
        }

        if (rb.skip(iso_len); rb.size() != iso.size()) {
                Trace(TRACE_LEVEL_ERROR, "ring buffer size %Iu != iso[] size %Iu", rb.size(), iso.size());
                return STATUS_INVALID_BUFFER_SIZE; 
        }

        return STATUS_SUCCESS;
}

/*
 * Layout: transfer buffer(IN only), usbip_iso_packet_descriptor[].
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto isoch_transfer(_In_ wsk_context &ctx, _In_ bool wsk_events, _In_ const header_ret_submit &ret, _Inout_ URB &urb)
{
        auto cnt = ret.number_of_packets;

	auto &r = urb.UrbIsochronousTransfer;
	r.ErrorCount = ret.error_count;

	if (cnt && cnt == ret.error_count) {
		r.Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r.TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r.StartFrame = ret.start_frame;
	}

	if (!(cnt >= 0 && static_cast<ULONG>(cnt) == r.NumberOfPackets)) {
                Trace(TRACE_LEVEL_ERROR, "number_of_packets %d != NumberOfPackets %lu", cnt, r.NumberOfPackets);
                return STATUS_INVALID_PARAMETER;
        }

        if (!(ret.actual_length >= 0 && static_cast<ULONG>(ret.actual_length) <= r.TransferBufferLength)) { // compacted
                Trace(TRACE_LEVEL_ERROR,"0 >= actual_length(%d) <= TransferBufferLength(%lu)",
                                         ret.actual_length, r.TransferBufferLength);
                return STATUS_INVALID_PARAMETER;
        }

        UCHAR *buffer;

        if (is_transfer_dir_out(ctx.hdr)) { // TransferFlags can have wrong direction
                buffer = nullptr;
        } else if (ULONG length; auto err = UdecxUrbRetrieveBuffer(ctx.request, &buffer, &length)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUrbRetrieveBuffer %!STATUS!", err);
                return err;
        }

        if (wsk_events) {
                return fill_isoc_data(r, buffer, ret.actual_length, ctx.dev->recv_buf);
        }

        NT_ASSERT(r.NumberOfPackets == number_of_packets(ctx));
        byteswap(ctx.isoc, cnt);

        return fill_isoc_data(r, buffer, ret.actual_length, ctx.isoc);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void post_control_transfer(_In_ const device_ctx &dev, _In_ const _URB_CONTROL_TRANSFER &r, _In_ void *TransferBuffer)
{
        NT_ASSERT(is_transfer_dir_in(r));

        auto dsc = static_cast<USB_COMMON_DESCRIPTOR*>(TransferBuffer);
	auto dsc_len = static_cast<UINT16>(r.TransferBufferLength);

	auto ok = (r.TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) &&
		   get_setup_packet(r).bRequest == USB_REQUEST_GET_DESCRIPTOR &&
		   dsc_len >= sizeof(*dsc);

	if (!ok) {
		return;
	}

	TraceUrb("bLength %d, %!usb_descriptor_type!%!BIN!", 
		  dsc->bLength, dsc->bDescriptorType, WppBinary(dsc, dsc_len));

	switch (dsc->bDescriptorType) {
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		if (auto &d = reinterpret_cast<USB_CONFIGURATION_DESCRIPTOR&>(*dsc);
		    dsc_len > sizeof(d) && d.bLength == sizeof(d) && d.wTotalLength == dsc_len) {
                        NT_ASSERT(libdrv::is_valid(d));
                        log(d);
                        if (dev.speed() < USB_SPEED_HIGH) {
                                patch_config(&d);
                        }
		}
		break;
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (auto &d = reinterpret_cast<USB_DEVICE_DESCRIPTOR&>(*dsc); 
		    dsc_len == sizeof(d) && d.bLength == dsc_len) {
                        NT_ASSERT(libdrv::is_valid(d));
                        log(d);
                        if (auto &props = dev.ext().properties(); *props.serial) {
                                if (!d.iSerialNumber) {
                                        d.iSerialNumber = MAXUCHAR; // max possible
                                }
                                props.iserial = d.iSerialNumber;
                        }
                }
                break;
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void post_process_transfer_buffer(_In_ const device_ctx &dev, _In_ const URB &urb, _In_ void *TransferBuffer)
{
	switch (urb.UrbHeader.Function) {
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
	case URB_FUNCTION_CONTROL_TRANSFER: // structures are binary compatible, see urbtransfer.cpp
		static_assert(sizeof(urb.UrbControlTransfer) == sizeof(urb.UrbControlTransferEx));
		post_control_transfer(dev, urb.UrbControlTransfer, TransferBuffer);
	}
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto ret_submit_urb(_Inout_ wsk_context &ctx, _In_ const header_ret_submit &ret, _Inout_ URB &urb)
{
        auto &dev = *ctx.dev;
        auto wsk_events = dev.wsk_events();

        urb.UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;

	if (is_isoch(urb)) {
		return isoch_transfer(ctx, wsk_events, ret, urb);
	}

        UCHAR *TransferBuffer{};
        ULONG TransferBufferLength{};

        if (auto err = UdecxUrbRetrieveBuffer(ctx.request, &TransferBuffer, &TransferBufferLength)) {
                return err == STATUS_INVALID_PARAMETER ? STATUS_SUCCESS : err; // OK if URB has no transfer buffer
        }

        TransferBufferLength = AsUrbTransfer(urb).TransferBufferLength; // ignore Length from UdecxUrbRetrieveBuffer

        if (TransferBufferLength != static_cast<ULONG>(ret.actual_length)) { // prepare_wsk_mdl can set it
		auto st = assign(TransferBufferLength, ret.actual_length); // DIR_OUT or !actual_length
		UdecxUrbSetBytesCompleted(ctx.request, TransferBufferLength);
                if (st) {
                        return st;
                }
	}

        if (TransferBufferLength && is_transfer_dir_in(ctx.hdr)) { // TransferFlags can have wrong direction
                if (wsk_events) {
                        ring_buffer rb(dev.recv_buf);
                        if (auto n = rb.read(TransferBuffer, TransferBufferLength); n != TransferBufferLength) {
                                Trace(TRACE_LEVEL_ERROR, "read %Iu != TransferBufferLength %lu", n, TransferBufferLength);
                                return STATUS_INVALID_BUFFER_SIZE;
                        }
                }
                post_process_transfer_buffer(dev, urb, TransferBuffer);
        }

        return STATUS_SUCCESS;
}

} // namespace


/*
 * For RET_UNLINK irp was completed right after CMD_UNLINK was issued.
 * @see send_cmd_unlink
 *
 * USBIP_RET_UNLINK
 * 1) if UNLINK is successful, status is -ECONNRESET
 * 2) if USBIP_CMD_UNLINK is after USBIP_RET_SUBMIT status is 0
 * See: <kernel>/Documentation/usb/usbip_protocol.rst
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::ret_command(_In_ const header &hdr, _Inout_ device_ctx &dev)
{
	auto request = hdr.command == RET_SUBMIT ? // request must be completed
		       device::begin_response(dev, hdr.seqnum) : WDF_NO_HANDLE;

	char buf[DBG_USBIP_HDR_BUFSZ];
	TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "req %04x <- %Iu%s", ptr04x(request), 
		    get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, false));

	return request;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::ret_submit(_Inout_ wsk_context &ctx)
{
        auto &ret = get_ret_submit(ctx.hdr);
        auto urb = try_get_urb(ctx.request); // IOCTL_INTERNAL_USB_SUBMIT_URB

        return  urb ? ret_submit_urb(ctx, ret, *urb) :
                ret.status ? STATUS_UNSUCCESSFUL : 
                STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::validate(_Inout_ header &hdr)
{
	byteswap_header(hdr, swap_dir::net2host);

	auto cmd = static_cast<request_type>(hdr.command);

	switch (cmd) {
	case RET_SUBMIT: {
		auto &ret = hdr.ret_submit;
		if (ret.number_of_packets == number_of_packets_non_isoch) {
			ret.number_of_packets = 0;
		} else if (!is_valid_number_of_packets(ret.number_of_packets)) {
			Trace(TRACE_LEVEL_ERROR, "number_of_packets(%d) is out of range", ret.number_of_packets);
			return false;
		}
	}	break;
	case RET_UNLINK:
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "USBIP_RET_* expected, got %!usbip_request_type!", cmd);
		return false;
	}

	auto ok = is_valid_seqnum(hdr.seqnum);

	if (ok) {
		hdr.direction = extract_dir(hdr.seqnum); // always zero in server response
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid seqnum %u", hdr.seqnum);
	}

	return ok;
}


/*
 * Publish a terminal request state. The device completion DPC performs the
 * actual UDE completion after both response processing and WskSend are done.
 * @see Write a UDE client driver
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::complete(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
	device::finish_request(request, status);
}

/*
 * UDE requires URBs to be completed from a separate DPC at DISPATCH_LEVEL.
 * This routine is called only by request_completion_dpc.
 */
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
void usbip::complete_now(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
	NT_ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	auto irp = WdfRequestWdmGetIrp(request);

	auto info = irp->IoStatus.Information;
	NT_ASSERT(info == WdfRequestGetInformation(request));

	auto &req = *get_request_ctx(request);

	if (!libdrv::has_urb(irp)) {
		if (status) {
			TraceUrb("seqnum %u, %!STATUS!, Information %#Ix", req.seqnum, status, info);
		}
		WdfRequestComplete(request, status);
		return;
	}

	auto &urb = *libdrv::urb_from_irp(irp);
	auto &urb_st = urb.UrbHeader.Status;

	if (status == STATUS_CANCELLED && urb_st == USBD_STATUS_PENDING) {
		urb_st = USBD_STATUS_CANCELED; // FIXME: is this really required?
	}

	if (status || urb_st) {
		TraceUrb("seqnum %u, USBD_%s, %!STATUS!, Information %#Ix", 
			  req.seqnum, get_usbd_status(urb_st), status, info);
	}

	if (NT_SUCCESS(status)) {
		UdecxUrbComplete(request, urb_st);
	} else {
		UdecxUrbCompleteWithNtStatus(request, status);
	}
}
