/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include "request.h"
#include "driver.h"

#include <libdrv/remove_lock.h>
#include <libdrv/usbd_helper.h>
#include <libdrv/dbgcommon.h>
#include <libdrv/ioctl.h>
#include <libdrv/utils.h>
#include <libdrv/select.h>
#include <libdrv/urb_ptr.h>

namespace
{

using namespace usbip;

enum { ARG_TAG, ARG_URB };

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS request_complete(
	_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
        NT_ASSERT(!devobj);
        auto &fltr = *static_cast<filter_ext*>(context);

        libdrv::RemoveLockGuard lck(fltr.remove_lock, libdrv::adopt_lock, libdrv::argv<ARG_TAG>(irp));
        NT_ASSERT(lck.tag() != irp);

        libdrv::irp_ptr rip(irp);
        libdrv::urb_ptr urb(fltr.device.usbd_handle, libdrv::argv<URB*, ARG_URB>(irp));

        TraceDbg("dev %04x, irp %04x -> target %04x, %!STATUS!, USBD_STATUS_%s", ptr04x(fltr.self), 
                  ptr04x(irp), ptr04x(fltr.target), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb.get())));

        unique_ptr{urb->UrbControlTransferEx.TransferBuffer};
        return StopCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto send_request(
	_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, 
	_Inout_ unique_ptr &TransferBuffer, _In_ USHORT function)
{
	auto target = fltr.target;

	libdrv::irp_ptr irp(target->StackSize, false);
	if (!irp) {
		Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp error");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

        auto next = IoGetNextIrpStackLocation(irp.get());

        next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        libdrv::DeviceIoControlCode(next) = IOCTL_INTERNAL_USB_SUBMIT_URB;

	libdrv::urb_ptr urb(fltr.device.usbd_handle);
	if (auto err = urb.alloc(next)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS!", err);
		return err;
	}

        if (auto err = IoSetCompletionRoutineEx(target, irp.get(), request_complete, &fltr, true, true, true)) {
                Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
                return err;
        }

        filter::pack_request(urb->UrbControlTransferEx, TransferBuffer.release(), function);
        libdrv::argv<ARG_URB>(irp.get()) = urb.release();
        libdrv::argv<ARG_TAG>(irp.get()) = lck.clear();

        TraceDbg("dev %04x, irp %04x -> target %04x", ptr04x(fltr.self), ptr04x(irp.get()), ptr04x(target));
        return IoCallDriver(target, irp.release()); // completion routine will be called anyway
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void send_urb(_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, _In_ const URB &urb)
{
	if (auto &hdr = urb.UrbHeader;
	    auto buf = unique_ptr(libdrv::uninitialized, NonPagedPoolNx, hdr.Length)) {
		RtlCopyMemory(buf.get(), &urb, hdr.Length);
		send_request(fltr, lck, buf, hdr.Function);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", hdr.Length);
	}
}

/*
 * @see drivers/usb/usbip/stub_rx.c, tweak_set_configuration_cmd
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void send_urb(_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, _In_ const _URB_SELECT_CONFIGURATION &r)
{
	{
		char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_configuration_str(buf, sizeof(buf), &r));
	}

        ULONG len{};
        if (unique_ptr buf(clone(len, r, NonPagedPoolNx, buf.pooltag)); buf) {
		send_request(fltr, lck, buf, r.Hdr.Function);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", len);
	}
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void post_process_urb(_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, _In_ const URB &urb)
{
	bool send{};
	
	switch (auto &hdr = urb.UrbHeader; hdr.Function) {
	using filter::is_request_function;
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
	case URB_FUNCTION_SYNC_RESET_PIPE:
	case URB_FUNCTION_SYNC_CLEAR_STALL:
		static_assert(is_request_function(URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL));
		static_assert(is_request_function(URB_FUNCTION_SYNC_RESET_PIPE));
		static_assert(is_request_function(URB_FUNCTION_SYNC_CLEAR_STALL));
		if constexpr (auto &r = urb.UrbPipeRequest; true) {
			TraceDbg("dev %04x, %s, PipeHandle %04x", ptr04x(fltr.self), 
				  urb_function_str(hdr.Function), ptr04x(r.PipeHandle));
		}
		send = true;
		break;
	case URB_FUNCTION_SELECT_INTERFACE:
		static_assert(is_request_function(URB_FUNCTION_SELECT_INTERFACE));
		if constexpr (auto &r = urb.UrbSelectInterface; true) {
			char buf[libdrv::SELECT_INTERFACE_STR_BUFSZ];
			TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_interface_str(buf, sizeof(buf), r));
		}
		send = true;
		break;
	case URB_FUNCTION_SELECT_CONFIGURATION:
		static_assert(is_request_function(URB_FUNCTION_SELECT_CONFIGURATION));
                if constexpr (auto &r = urb.UrbSelectConfiguration; true) {
                        char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
                        TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_configuration_str(buf, sizeof(buf), &r));
                }
                send_urb(fltr, lck, urb.UrbSelectConfiguration);
		break;
	default:
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), urb_function_str(hdr.Function));
	}

	if (send) {
		send_urb(fltr, lck, urb);
	}
}

/*
// _URB_CONTROL_DESCRIPTOR_REQUEST 
case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
//
case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:

// _URB_CONTROL_FEATURE_REQUEST
case URB_FUNCTION_SET_FEATURE_TO_DEVICE:
case URB_FUNCTION_SET_FEATURE_TO_INTERFACE:
case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT:
case URB_FUNCTION_SET_FEATURE_TO_OTHER:
//
case URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE:
case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT:
case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER:

// _URB_CONTROL_GET_STATUS_REQUEST 
case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
case URB_FUNCTION_GET_STATUS_FROM_OTHER:

case URB_FUNCTION_GET_CONFIGURATION: // _URB_CONTROL_GET_CONFIGURATION_REQUEST 
case URB_FUNCTION_GET_INTERFACE: // _URB_CONTROL_GET_INTERFACE_REQUEST 

case URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR: // _URB_OS_FEATURE_DESCRIPTOR_REQUEST
*/
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto get_request_type(_In_ const URB &urb)
{
        UCHAR bmRequestType;

        switch (urb.UrbHeader.Function) {
        case URB_FUNCTION_VENDOR_DEVICE:
                bmRequestType = USB_TYPE_VENDOR | USB_RECIP_DEVICE;
                break;
        case URB_FUNCTION_VENDOR_INTERFACE:
                bmRequestType = USB_TYPE_VENDOR | USB_RECIP_INTERFACE;
                break;
        case URB_FUNCTION_VENDOR_ENDPOINT:
                bmRequestType = USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
                break;
        case URB_FUNCTION_VENDOR_OTHER:
                bmRequestType = USB_TYPE_VENDOR | USB_RECIP_OTHER;
                break;
        case URB_FUNCTION_CLASS_DEVICE:
                bmRequestType = USB_TYPE_CLASS | USB_RECIP_DEVICE;
                break;
        case URB_FUNCTION_CLASS_INTERFACE:
                bmRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
                break;
        case URB_FUNCTION_CLASS_ENDPOINT:
                bmRequestType = USB_TYPE_CLASS | USB_RECIP_ENDPOINT;
                break;
        case URB_FUNCTION_CLASS_OTHER:
                bmRequestType = USB_TYPE_CLASS | USB_RECIP_OTHER;
                break;
        default:
                return UCHAR{};
        }

        NT_ASSERT(bmRequestType);

        auto dir_in = IsTransferDirectionIn(urb.UrbControlVendorClassRequest.TransferFlags);
        bmRequestType |= (dir_in ? USB_DIR_IN : USB_DIR_OUT);

        return bmRequestType;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void vendor_class_to_control(_Inout_ URB &urb, _In_ UCHAR bmRequestType)
{
        auto &d = urb.UrbControlTransfer;
        auto &s = urb.UrbControlVendorClassRequest;

        static_assert(sizeof(d) == sizeof(s));
        NT_ASSERT(d.Hdr.Length == sizeof(d));

        d.Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER;

        NT_ASSERT(!d.PipeHandle); // s.Reserved
        d.TransferFlags |= USBD_DEFAULT_PIPE_TRANSFER;

        NT_ASSERT(!s.RequestTypeReservedBits);
        s.RequestTypeReservedBits = bmRequestType;

        NT_ASSERT(!s.Reserved1);
        s.Reserved1 = static_cast<USHORT>(s.TransferBufferLength); // get_setup_packet(d).wLength
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void control_to_vendor_class(_Inout_ URB &urb, _In_ USHORT function)
{
        auto &r = urb.UrbControlVendorClassRequest;
        r.Hdr.Function = function;
        r.TransferFlags &= ~USBD_DEFAULT_PIPE_TRANSFER; // clear flag
        r.RequestTypeReservedBits = 0;
        r.Reserved1 = 0;
}

/*
 * IRP -> usbip2_filter.sys -> ucx01000.sys -> udecx.sys -> usbip2_ude.sys
 * IRP can be completed by ucx01000 or udecx, in such case usbip2_ude will not receive it.
 * To detect such issues, all IRPs are inspected upon completion.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void post_process_irp(_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, _In_ IRP *irp)
{
        auto status = irp->IoStatus.Status;

        if (auto ctl = libdrv::DeviceIoControlCode(irp); ctl != IOCTL_INTERNAL_USB_SUBMIT_URB) {
                TraceDbg("dev %04x, %s, %!STATUS!", ptr04x(fltr.self), internal_device_control_name(ctl), status);

        } else if (auto urb = libdrv::urb_from_irp(irp); NT_ERROR(status) || USBD_ERROR(URB_STATUS(urb))) {
                auto &hdr = urb->UrbHeader;
                Trace(TRACE_LEVEL_ERROR, "dev %04x, %s, USBD_STATUS_%s, %!STATUS!", ptr04x(fltr.self), 
                        urb_function_str(hdr.Function), get_usbd_status(hdr.Status), status);
        } else {
                post_process_urb(fltr, lck, *urb);
        }
}

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS irp_complete(
        _In_ DEVICE_OBJECT *devobj, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
        auto &fltr = *get_filter_ext(devobj);
        libdrv::RemoveLockGuard lck(fltr.remove_lock, libdrv::adopt_lock, irp);

        if (auto function = static_cast<USHORT>(reinterpret_cast<uintptr_t>(context))) { // legacy control transfer

                auto urb = libdrv::urb_from_irp(irp);
                control_to_vendor_class(*urb, function);

                TraceDbg("dev %04x, irp %04x, %!STATUS!, USBD_STATUS_%s", ptr04x(fltr.self),
                          ptr04x(irp), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb)));
        } else {
                post_process_irp(fltr, lck, irp);
        }

        if (irp->PendingReturned) {
                IoMarkIrpPending(irp);
        }

        return ContinueCompletion;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void *try_legacy_ctrl(_In_ filter_ext &fltr, _In_ IRP *irp, _Inout_ URB &urb)
{
        auto bmRequestType = get_request_type(urb);
        if (!bmRequestType) {
                return nullptr;
        }

        auto function = urb.UrbHeader.Function;
        NT_ASSERT(function);

        TraceDbg("dev %04x, irp %04x -> target %04x, %s", ptr04x(fltr.self), ptr04x(irp),
                  ptr04x(fltr.target), urb_function_str(function));

        vendor_class_to_control(urb, bmRequestType); // changes function
        return reinterpret_cast<void*>(static_cast<uintptr_t>(function));
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
NTSTATUS usbip::int_dev_ctrl(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	auto &fltr = *get_filter_ext(devobj);
        if (fltr.is_hub) {
                return ForwardIrp(fltr, irp);
        }

	libdrv::RemoveLockGuard lck(fltr.remove_lock, irp);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

        auto ctx = libdrv::has_urb(irp) ?
                   try_legacy_ctrl(fltr, irp, *libdrv::urb_from_irp(irp)) : nullptr;

        IoCopyCurrentIrpStackLocationToNext(irp);

        if (auto err = IoSetCompletionRoutineEx(fltr.target, irp, irp_complete, ctx, true, true, true)) {
                Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
                IoSkipCurrentIrpStackLocation(irp); // forward and forget
        } else {
                lck.clear();
        }

        return IoCallDriver(fltr.target, irp);
}
