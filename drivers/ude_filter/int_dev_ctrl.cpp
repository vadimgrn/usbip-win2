/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include "request.h"
#include "driver.h"
#include "smartptr.h"

#include <usbip\ch9.h>
#include <libdrv\remove_lock.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\ioctl.h>
#include <libdrv\select.h>

namespace
{

using namespace usbip;

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_send_request(
	_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	auto urb = libdrv::argv<0, URB>(irp);

	TraceDbg("dev %04x, urb %04x -> target %04x, %!STATUS!, USBD_STATUS_%s", ptr04x(fltr.self), 
		  ptr04x(urb), ptr04x(fltr.target), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb)));

	if (auto &r = urb->UrbControlTransferEx; r.TransferBuffer) {
		ExFreePoolWithTag(r.TransferBuffer, unique_ptr::pooltag);
	}

	if (auto &handle = fltr.dev.usbd) {
		USBD_UrbFree(handle, urb);
	} else {
		NT_ASSERT(!"USBD_HANDLE is NULL");
	}
	
	IoFreeIrp(irp);
	return StopCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto send_request(_In_ filter_ext &fltr, _In_ void *TransferBuffer, _In_ bool cfg_or_intf)
{
	auto &target = fltr.target;

	irp_ptr irp(target->StackSize, false);
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

	urb_ptr urb(fltr.dev.usbd);
	if (auto err = urb.alloc(next_stack)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS!", err);
		return err;
	}

	filter::pack_request_select(urb.get()->UrbControlTransferEx, TransferBuffer, cfg_or_intf);
	TraceDbg("dev %04x, urb %04x -> target %04x", ptr04x(fltr.self), ptr04x(urb.get()), ptr04x(target));

	libdrv::argv<0>(irp.get()) = urb.get();
	auto st = IoCallDriver(target, irp.get());

	if (NT_ERROR(st)) {
		Trace(TRACE_LEVEL_ERROR, "IoCallDriver %!STATUS!", st);
	} else {
		urb.release();
		irp.release();
	}

	return st;
}

/*
 * @see drivers/usb/usbip/stub_rx.c, tweak_set_configuration_cmd
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void select_configuration(_In_ filter_ext &fltr, _In_ _URB_SELECT_CONFIGURATION &r)
{
	{
		char buf[libdrv::SELECT_CONFIGURATION_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_configuration_str(buf, sizeof(buf), &r));
	}
	
	ULONG len{};
	
	if (unique_ptr buf = clone(len, r, POOL_FLAG_NON_PAGED, unique_ptr::pooltag); !buf) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", len);
	} else if (NT_SUCCESS(send_request(fltr, buf.get(), true))) {
		buf.release();
	}
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void select_interface(_In_ filter_ext &fltr, _In_ _URB_SELECT_INTERFACE &r)
{
	{
		char buf[libdrv::SELECT_INTERFACE_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), libdrv::select_interface_str(buf, sizeof(buf), r));
	}

	if (unique_ptr buf = clone(r, POOL_FLAG_NON_PAGED, unique_ptr::pooltag); !buf) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", r.Hdr.Length);
	} else if (NT_SUCCESS(send_request(fltr, buf.get(), false))) {
		buf.release();
	}
}

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_select(_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	auto &urb = *libdrv::urb_from_irp(irp);

	auto &hdr = urb.UrbHeader;
	auto &func = hdr.Function;

	if (auto st = irp->IoStatus.Status; NT_ERROR(st) || USBD_ERROR(hdr.Status)) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, %s, %!STATUS!, USBD_STATUS_%s", 
			ptr04x(fltr.self), urb_function_str(func), st, get_usbd_status(hdr.Status));
	} else if (func == URB_FUNCTION_SELECT_INTERFACE) {
		select_interface(fltr, urb.UrbSelectInterface);
	} else {
		NT_ASSERT(func == URB_FUNCTION_SELECT_CONFIGURATION);
		select_configuration(fltr, urb.UrbSelectConfiguration);
	}

	if (irp->PendingReturned) {
		IoMarkIrpPending(irp);
	}

	return ContinueCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto handle_select(_In_ filter_ext &fltr, _In_ IRP *irp)
{
	IoCopyCurrentIrpStackLocationToNext(irp);

	if (auto err = IoSetCompletionRoutineEx(fltr.target, irp, on_select, &fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		IoSkipCurrentIrpStackLocation(irp); // forward and forget
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

	libdrv::RemoveLockGuard lck(fltr.remove_lock);
	if (auto err = lck.acquired()) {
		Trace(TRACE_LEVEL_ERROR, "Acquire remove lock %!STATUS!", err);
		return CompleteRequest(irp, err);
	}

	if (!fltr.is_hub && libdrv::DeviceIoControlCode(irp) == IOCTL_INTERNAL_USB_SUBMIT_URB) {

		switch (auto urb = libdrv::urb_from_irp(irp); urb->UrbHeader.Function) {
		case URB_FUNCTION_SELECT_INTERFACE:
		case URB_FUNCTION_SELECT_CONFIGURATION: 
			return handle_select(fltr, irp);
		}
	}

	return ForwardIrp(fltr, irp);
}
