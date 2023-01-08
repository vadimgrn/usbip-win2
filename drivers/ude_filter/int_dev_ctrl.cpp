/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include "device.h"
#include "select.h"

#include <usbip\ch9.h>
#include <libdrv\remove_lock.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\ioctl.h>

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


_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void init(_Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ bool cfg_or_if, _In_ UCHAR value, _In_ UCHAR index)
{
	if (auto h = &r.Hdr) {
		h->Length = sizeof(r);
		h->Function = URB_FUNCTION_CONTROL_TRANSFER_EX;
		h->Status = USBD_STATUS_SUCCESS;
	}

	r.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
	
	auto &pkt = get_setup_packet(r);
	pkt.bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | 
		              UCHAR(cfg_or_if ? USB_RECIP_DEVICE : USB_RECIP_INTERFACE);

	pkt.bRequest = cfg_or_if ? USB_REQUEST_SET_CONFIGURATION : USB_REQUEST_SET_INTERFACE;
	pkt.wValue.W = value;
	pkt.wIndex.W = index;
}

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_set_request(
	_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	auto urb = argv<0, URB>(irp);

	TraceDbg("dev %04x, irp %04x, %!STATUS!, USBD_STATUS_%s", 
		  ptr04x(fltr.self), ptr04x(irp), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb)));

	USBD_UrbFree(fltr.dev.usbd, urb);
	IoFreeIrp(irp);

	return StopCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto send_set_request(_In_ filter_ext &fltr, _In_ bool cfg_or_if, _In_ UCHAR value, _In_ UCHAR index)
{
	auto &target = fltr.target;

	irp_ptr irp(target->StackSize, false);
	if (!irp) {
		Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp error");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto next_stack = IoGetNextIrpStackLocation(irp.get());

	next_stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	libdrv::DeviceIoControlCode(irp.get(), next_stack) = IOCTL_INTERNAL_USB_SUBMIT_URB;

	if (auto err = IoSetCompletionRoutineEx(target, irp.get(), on_set_request, &fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		return err;
	}

	auto &usbd = fltr.dev.usbd;

	URB *urb{};
	if (auto err = USBD_UrbAllocate(usbd, &urb)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS!", err);
		return err;
	}

	USBD_AssignUrbToIoStackLocation(usbd, next_stack, urb);
	init(urb->UrbControlTransferEx, cfg_or_if, value, index);

	TraceDbg("dev %04x, irp %04x", ptr04x(fltr.self), ptr04x(irp.get()));

	argv<0>(irp.get()) = urb;
	auto st = IoCallDriver(target, irp.get());

	if (NT_ERROR(st)) {
		Trace(TRACE_LEVEL_ERROR, "IoCallDriver %!STATUS!", st);
		USBD_UrbFree(usbd, urb);
	} else {
		irp.release();
	}

	return st;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void on_select_cfg(_In_ filter_ext &fltr, _In_ const _URB_SELECT_CONFIGURATION &r)
{
	char buf[SELECT_CONFIGURATION_STR_BUFSZ];
	TraceDbg("dev %04x, %s", ptr04x(fltr.self), select_configuration_str(buf, sizeof(buf), &r));

	auto cd = r.ConfigurationDescriptor; // null if unconfigured
	UCHAR value = cd ? cd->bConfigurationValue : 0; // FIXME: -1?

	send_set_request(fltr, true, value, 0);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void on_select_intf(_In_ filter_ext &fltr, _In_ const _URB_SELECT_INTERFACE &intf)
{
	char buf[SELECT_INTERFACE_STR_BUFSZ];
	TraceDbg("dev %04x, %s", ptr04x(fltr.self), select_interface_str(buf, sizeof(buf), intf));

	auto &r = intf.Interface;
	send_set_request(fltr, false, r.AlternateSetting, r.InterfaceNumber);
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

	if (auto st = irp->IoStatus.Status; !(NT_SUCCESS(st) && USBD_SUCCESS(hdr.Status))) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, %s, %!STATUS!, USBD_STATUS_%s", 
			ptr04x(fltr.self), urb_function_str(func), st, get_usbd_status(hdr.Status));

	} else if (func == URB_FUNCTION_SELECT_INTERFACE) {
		on_select_intf(fltr, urb.UrbSelectInterface);
	} else {
		NT_ASSERT(func == URB_FUNCTION_SELECT_CONFIGURATION);
		on_select_cfg(fltr, urb.UrbSelectConfiguration);
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
