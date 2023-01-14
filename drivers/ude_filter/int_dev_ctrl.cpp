/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include "request.h"
#include "driver.h"

#include <usbip\ch9.h>
#include <libdrv\remove_lock.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\ioctl.h>
#include <libdrv\select.h>

namespace
{

using namespace usbip;

class irp_ptr
{
public:
	template<typename ...Args>
	irp_ptr(Args&&... args) : m_irp(IoAllocateIrp(args...)) {}

	~irp_ptr()
	{
		if (m_irp) {
			IoFreeIrp(m_irp);
		}
	}

	explicit operator bool() const { return m_irp; }
	auto operator !() const { return !m_irp; }

	auto get() const { return m_irp; }
	void release() { m_irp = nullptr; }

private:
	IRP *m_irp;
};

class urb_ptr
{
public:
	urb_ptr(_In_ USBD_HANDLE handle) : m_handle(handle) { NT_ASSERT(m_handle); }

	~urb_ptr()
	{
		if (m_urb) {
			USBD_UrbFree(m_handle, m_urb);
		}
	}

	auto alloc(_In_ IO_STACK_LOCATION *stack)
	{
		auto st = m_urb ? STATUS_ALREADY_INITIALIZED : USBD_UrbAllocate(m_handle, &m_urb); 
		if (NT_SUCCESS(st)) {
			USBD_AssignUrbToIoStackLocation(m_handle, stack, m_urb);
		}
		return st;
	}

	auto get() const { return m_urb; }
	void release() { m_urb = nullptr; }

private:
	USBD_HANDLE m_handle{};
	URB *m_urb{};
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto clone(_In_ const _URB_SELECT_INTERFACE &r, _In_ POOL_FLAGS Flags, _In_ ULONG PoolTag)
{
	auto len = r.Hdr.Length;

	auto ptr = (_URB_SELECT_INTERFACE*)ExAllocatePool2(Flags, len, PoolTag);
	if (ptr) {
		RtlCopyMemory(ptr, &r, len);
	}

	return ptr;
}

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

	{
		auto &r = urb->UrbControlTransferEx;
		auto &pkt = get_setup_packet(r); 
		
		if (pkt.bRequest == USB_REQUEST_SET_CONFIGURATION) {
			auto ptr = static_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer);
			libdrv::free(ptr, unique_ptr::pooltag);
		} else {
			NT_ASSERT(pkt.bRequest == USB_REQUEST_SET_INTERFACE);
			NT_ASSERT(r.TransferBuffer);
			ExFreePoolWithTag(r.TransferBuffer, unique_ptr::pooltag);
		}
	}

	if (auto &handle = fltr.dev.usbd) {
		USBD_UrbFree(handle, urb);
	} else {
		NT_ASSERT(!"fltr.dev.usbd");
	}
	
	IoFreeIrp(irp);
	return StopCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto send_request(
	_In_ filter_ext &fltr, _In_ bool cfg_or_intf, 
	_In_ void *TransferBuffer, _In_ ULONG TransferBufferLength)
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

	filter::pack_request_select(urb.get()->UrbControlTransferEx, 
		                    cfg_or_intf, TransferBuffer, TransferBufferLength);

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
		char buf[SELECT_CONFIGURATION_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), select_configuration_str(buf, sizeof(buf), &r));
	}

	unique_ptr buf = libdrv::clone(r, POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, unique_ptr::pooltag);

	if (auto len = r.Hdr.Length; !buf) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", len);
	} else if (NT_SUCCESS(send_request(fltr, true, buf.get(), len))) {
		buf.release();
	}
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void select_interface(_In_ filter_ext &fltr, _In_ _URB_SELECT_INTERFACE &r)
{
	{
		char buf[SELECT_INTERFACE_STR_BUFSZ];
		TraceDbg("dev %04x, %s", ptr04x(fltr.self), select_interface_str(buf, sizeof(buf), r));
	}

	unique_ptr buf = clone(r, POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, unique_ptr::pooltag);

	if (auto len = r.Hdr.Length; !buf) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", len);
	} else if (NT_SUCCESS(send_request(fltr, false, buf.get(), len))) {
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
