/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include "request.h"
#include "driver.h"

#include <libdrv\remove_lock.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\ioctl.h>
#include <libdrv\select.h>
#include <libdrv\urb_ptr.h>

namespace
{

using namespace usbip;

enum { ARG_URB, ARG_TAG };

/*
 * IRP context slots used on the caller's IRP when translating a legacy vendor/class URB
 * into URB_FUNCTION_CONTROL_TRANSFER_EX form. Independent of ARG_URB / ARG_TAG above,
 * which are used on a synthetic IRP allocated inside send_request().
 *
 * ARG_TRANSLATE_ORIG_URB: pointer to the caller's original _URB_CONTROL_VENDOR_OR_CLASS_REQUEST
 *                        URB. Status fields are copied back on completion.
 * ARG_TRANSLATE_EX_URB:   pointer to the URB_FUNCTION_CONTROL_TRANSFER_EX URB we allocated
 *                        and assigned to the next IRP stack location. Freed on completion.
 */
enum { ARG_TRANSLATE_ORIG_URB = 2, ARG_TRANSLATE_EX_URB = 3 };

/*
 * @param result of make_irp() 
 * IO_REMOVE_LOCK must be used because IRP_MN_REMOVE_DEVICE can remove FiDO prior this callback.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto free(_Inout_ filter_ext &fltr, _In_ IRP *irp)
{
	auto urb = libdrv::argv<URB*, ARG_URB>(irp);
	NT_ASSERT(urb);

	auto tag = libdrv::argv<ARG_TAG>(irp);
	NT_ASSERT(tag);

	TraceDbg("dev %04x, urb %04x -> target %04x, %!STATUS!, USBD_STATUS_%s", ptr04x(fltr.self), 
		  ptr04x(urb), ptr04x(fltr.target), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb)));

	{
		libdrv::urb_ptr a(fltr.device.usbd_handle, urb);
		unique_ptr b(urb->UrbControlTransferEx.TransferBuffer);
	}

	IoFreeIrp(irp);
	return tag;
}

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_send_request(
	_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);

	auto tag = free(fltr, irp);
	libdrv::RemoveLockGuard guard(fltr.remove_lock, libdrv::adopt_lock, tag);

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

	auto next_stack = IoGetNextIrpStackLocation(irp.get());

	next_stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	libdrv::DeviceIoControlCode(next_stack) = IOCTL_INTERNAL_USB_SUBMIT_URB;

	if (auto err = IoSetCompletionRoutineEx(target, irp.get(), on_send_request, &fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		return err;
	}

	libdrv::urb_ptr urb(fltr.device.usbd_handle);
	if (auto err = urb.alloc(next_stack)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS!", err);
		return err;
	}

	filter::pack_request(urb.get()->UrbControlTransferEx, TransferBuffer.release(), function);
	TraceDbg("dev %04x, urb %04x -> target %04x", ptr04x(fltr.self), ptr04x(urb.get()), ptr04x(target));

	libdrv::argv<ARG_URB>(irp.get()) = urb.release();

	libdrv::argv<ARG_TAG>(irp.get()) = lck.tag();
	lck.clear();

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
void select_configuration(_In_ filter_ext &fltr, _Inout_ libdrv::RemoveLockGuard &lck, _In_ const _URB_SELECT_CONFIGURATION &r)
{
	{
		char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_configuration_str(buf, sizeof(buf), &r));
	}

	if (ULONG len{}; unique_ptr buf = clone(len, r, NonPagedPoolNx, buf.pooltag)) {
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
		select_configuration(fltr, lck, urb.UrbSelectConfiguration);
		break;
	default:
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), urb_function_str(hdr.Function));
	}

	if (send) {
		send_urb(fltr, lck, urb);
	}
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
NTSTATUS irp_completed(_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	libdrv::RemoveLockGuard lck(fltr.remove_lock, libdrv::adopt_lock, irp);

	post_process_irp(fltr, lck, irp);

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	return ContinueCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto pre_process_irp(_In_ filter_ext &fltr, _In_ IRP *irp, _Inout_ libdrv::RemoveLockGuard &lck)
{
	IoCopyCurrentIrpStackLocationToNext(irp);

	if (auto err = IoSetCompletionRoutineEx(fltr.target, irp, irp_completed, &fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		IoSkipCurrentIrpStackLocation(irp); // forward and forget
	} else {
		lck.clear();
	}

	return IoCallDriver(fltr.target, irp);
}

/*
 * Completion routine for IRPs whose URB was translated from a legacy vendor/class form
 * into URB_FUNCTION_CONTROL_TRANSFER_EX in translate_legacy_urb_pre_process(). Copies the
 * response status and actual transfer length from our EX URB back into the caller's
 * original URB, then frees the EX URB.
 *
 * The IO Manager has popped the stack back to our filter's stack location by the time
 * this routine runs, so urb_from_irp(irp) returns the original URB.
 */
_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS translate_irp_completed(
	_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	libdrv::RemoveLockGuard lck(fltr.remove_lock, libdrv::adopt_lock, irp);

	auto orig_urb = libdrv::argv<URB*, ARG_TRANSLATE_ORIG_URB>(irp);
	auto ex_urb   = libdrv::argv<URB*, ARG_TRANSLATE_EX_URB>(irp);

	NT_ASSERT(orig_urb);
	NT_ASSERT(ex_urb);

	const auto status   = irp->IoStatus.Status;
	const auto ex_ustat = ex_urb->UrbHeader.Status;
	const auto ex_len   = ex_urb->UrbControlTransferEx.TransferBufferLength;

	// Mirror the response back into the original URB so the upper driver sees a
	// well-formed completed legacy URB. UrbControlVendorClassRequest and
	// UrbControlTransferEx share the URB union; we write through the original
	// URB's vendor-class view explicitly to keep the source of truth tied to
	// the struct the caller originally built.
	orig_urb->UrbHeader.Status                        = ex_ustat;
	orig_urb->UrbControlVendorClassRequest.TransferBufferLength = ex_len;

	if (NT_ERROR(status) || USBD_ERROR(ex_ustat)) {
		Trace(TRACE_LEVEL_ERROR,
			"dev %04x, %s (translated), USBD_STATUS_%s, %!STATUS!",
			ptr04x(fltr.self), urb_function_str(orig_urb->UrbHeader.Function),
			get_usbd_status(ex_ustat), status);
	} else {
		TraceDbg("dev %04x, %s (translated), bytes %lu",
			ptr04x(fltr.self), urb_function_str(orig_urb->UrbHeader.Function), ex_len);
	}

	USBD_UrbFree(fltr.device.usbd_handle, ex_urb);

	libdrv::argv<ARG_TRANSLATE_ORIG_URB>(irp) = nullptr;
	libdrv::argv<ARG_TRANSLATE_EX_URB>(irp)   = nullptr;

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	return ContinueCompletion;
}

/*
 * Translate a legacy vendor/class URB (URB_FUNCTION_VENDOR_*/CLASS_*) into the
 * URB_FUNCTION_CONTROL_TRANSFER_EX form UDECX accepts, swap it into the next IRP
 * stack location, and forward. On completion translate_irp_completed restores the
 * caller's view. See issue #167.
 *
 * If allocation fails we fall through to standard forwarding — the URB will be
 * rejected by UDECX as before, matching pre-fix behaviour with no regression.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto translate_legacy_urb_pre_process(
	_In_ filter_ext &fltr, _In_ IRP *irp, _Inout_ libdrv::RemoveLockGuard &lck)
{
	auto orig_urb = libdrv::urb_from_irp(irp);
	NT_ASSERT(orig_urb);
	NT_ASSERT(filter::is_legacy_vendor_class_function(orig_urb->UrbHeader.Function));

	auto &src = orig_urb->UrbControlVendorClassRequest;

	// wLength is a 16-bit field in the setup packet; a control transfer cannot exceed it.
	if (src.TransferBufferLength > MAXUSHORT) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, %s: TransferBufferLength %lu exceeds MAXUSHORT",
			ptr04x(fltr.self), urb_function_str(orig_urb->UrbHeader.Function),
			src.TransferBufferLength);
		orig_urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
		return CompleteRequest(irp, STATUS_INVALID_PARAMETER);
	}

	IoCopyCurrentIrpStackLocationToNext(irp);
	auto next_stack = IoGetNextIrpStackLocation(irp);

	libdrv::urb_ptr ex_urb(fltr.device.usbd_handle);
	if (auto err = ex_urb.alloc(next_stack)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS! — falling back to unmodified forward",
			err);
		// Fall back: forward the original URB. UDECX will reject it but we preserve
		// existing behaviour and the caller sees the same error it would have without us.
		if (auto routine_err = IoSetCompletionRoutineEx(fltr.target, irp, irp_completed,
								&fltr, true, true, true)) {
			Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", routine_err);
			IoSkipCurrentIrpStackLocation(irp);
		} else {
			lck.clear();
		}
		return IoCallDriver(fltr.target, irp);
	}

	filter::translate_legacy_vendor_class(ex_urb.get()->UrbControlTransferEx, src);

	TraceDbg("dev %04x, %s -> CONTROL_TRANSFER_EX, len %lu, dir %s",
		ptr04x(fltr.self), urb_function_str(orig_urb->UrbHeader.Function),
		src.TransferBufferLength,
		(src.TransferFlags & USBD_TRANSFER_DIRECTION_IN) ? "IN" : "OUT");

	libdrv::argv<ARG_TRANSLATE_ORIG_URB>(irp) = orig_urb;
	libdrv::argv<ARG_TRANSLATE_EX_URB>(irp)   = ex_urb.release();

	if (auto err = IoSetCompletionRoutineEx(fltr.target, irp, translate_irp_completed,
						&fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS! (translate path)", err);
		// We never forwarded the IRP, so no lower driver will dereference the next
		// stack location's URB pointer. Still, restore orig_urb into next_stack's
		// Parameters.Others.Argument1 before freeing the EX URB, so we don't leave
		// a dangling pointer that driver-verifier would flag.
		auto ex_to_free = libdrv::argv<URB*, ARG_TRANSLATE_EX_URB>(irp);
		if (ex_to_free) {
			next_stack->Parameters.Others.Argument1 = orig_urb;
			USBD_UrbFree(fltr.device.usbd_handle, ex_to_free);
			libdrv::argv<ARG_TRANSLATE_EX_URB>(irp)   = nullptr;
			libdrv::argv<ARG_TRANSLATE_ORIG_URB>(irp) = nullptr;
		}
		orig_urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
		return CompleteRequest(irp, err);
	}

	lck.clear();
	return IoCallDriver(fltr.target, irp);
}

} // namespace


_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
NTSTATUS usbip::int_dev_ctrl(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	auto &fltr = *get_filter_ext(devobj);

	libdrv::RemoveLockGuard lck(fltr.remove_lock, irp);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

	if (fltr.is_hub) {
		return ForwardIrp(fltr, irp);
	}

	// Route legacy vendor/class URBs through the translation path so UDECX accepts them.
	// See issue #167 / ftdibus.sys et al.
	if (libdrv::has_urb(irp)) {
		if (auto urb = libdrv::urb_from_irp(irp);
		    urb && filter::is_legacy_vendor_class_function(urb->UrbHeader.Function)) {
			return translate_legacy_urb_pre_process(fltr, irp, lck);
		}
	}

	return pre_process_irp(fltr, irp, lck);
}
