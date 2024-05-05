/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

	auto &r = urb->UrbControlTransferEx;
	unique_ptr(r.TransferBuffer);

	libdrv::urb_ptr(fltr.device.usbd_handle, urb);
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
	libdrv::RemoveLockGuard(fltr.remove_lock, libdrv::adopt_lock, tag);

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

	return fltr.is_hub ? ForwardIrp(fltr, irp) : pre_process_irp(fltr, irp, lck);
}
