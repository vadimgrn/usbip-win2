/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "device.h"
#include "irp.h"

#include <usbip\ch9.h>
#include <libdrv\remove_lock.h>
#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>

#include <usbioctl.h>

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


auto& get_ioctl(_In_ IRP *irp, _In_ IO_STACK_LOCATION *stack = nullptr)
{
	if (!stack) {
		stack = IoGetCurrentIrpStackLocation(irp);
	}

	return stack->Parameters.DeviceIoControl.IoControlCode;
}

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
	r.Timeout = 5000; // milliseconds
	
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
NTSTATUS on_completion(
	_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	auto &fltr = *static_cast<filter_ext*>(context);
	NT_ASSERT(devobj->AttachedDevice == fltr.self);

	auto urb = argv<0, URB>(irp);

	TraceDbg("dev %04x, irp %04x, %!STATUS!, USBD_STATUS_%s", 
		  ptr04x(fltr.self), ptr04x(irp), irp->IoStatus.Status, get_usbd_status(URB_STATUS(urb)));

	USBD_UrbFree(fltr.dev.usbd, urb);
	IoFreeIrp(irp);

	return StopCompletion;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void send_request(_In_ filter_ext &fltr, _In_ bool cfg_or_if, _In_ UCHAR value, _In_ UCHAR index)
{
	auto &target = fltr.target;

	irp_ptr irp(target->StackSize, false);
	if (!irp) {
		Trace(TRACE_LEVEL_ERROR, "IoAllocateIrp error");
		return;
	}

	auto next_stack = IoGetNextIrpStackLocation(irp.get());

	next_stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	get_ioctl(irp.get(), next_stack) = IOCTL_INTERNAL_USB_SUBMIT_URB;

	if (auto err = IoSetCompletionRoutineEx(target, irp.get(), on_completion, &fltr, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		return;
	}

	auto &usbd = fltr.dev.usbd;

	URB *urb{};
	if (auto err = USBD_UrbAllocate(usbd, &urb)) {
		Trace(TRACE_LEVEL_ERROR, "USBD_UrbAllocate %!STATUS!", err);
		return;
	}

	USBD_AssignUrbToIoStackLocation(usbd, next_stack, urb);
	argv<0>(irp.get()) = urb;

	init(urb->UrbControlTransferEx, cfg_or_if, value, index);
	TraceDbg("dev %04x, irp %04x", ptr04x(fltr.self), ptr04x(irp.get()));

	if (auto st = IoCallDriver(target, irp.get()); NT_ERROR(st)) {
		Trace(TRACE_LEVEL_ERROR, "IoCallDriver %!STATUS!", st);
		USBD_UrbFree(usbd, urb);
	} else {
		irp.release();
	}
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void post_select_cfg(_In_ filter_ext &fltr, _In_ const _URB_SELECT_CONFIGURATION &r)
{
	auto cd = r.ConfigurationDescriptor; // null if unconfigured
	UCHAR value = cd ? cd->bConfigurationValue : 0;

	TraceDbg("dev %04x, ConfigurationHandle %04x, bConfigurationValue %d", 
		  ptr04x(fltr.self), ptr04x(r.ConfigurationHandle), value);

	send_request(fltr, true, value, 0);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void post_select_intf(_In_ filter_ext &fltr, _In_ const _URB_SELECT_INTERFACE &intf)
{
	auto &r = intf.Interface;

	TraceDbg("dev %04x, ConfigurationHandle %04x, InterfaceNumber %d, AlternateSetting %d", 
		  ptr04x(fltr.self), ptr04x(intf.ConfigurationHandle),
		  r.InterfaceNumber, r.AlternateSetting);

	send_request(fltr, false, r.AlternateSetting, r.InterfaceNumber);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
auto handle_select(_In_ filter_ext &fltr, _In_ IRP *irp, _In_ const URB &urb)
{
	auto &hdr = urb.UrbHeader;
	auto &func = hdr.Function;

	auto st = ForwardIrpAndWait(fltr, irp);

	if (!(NT_SUCCESS(st) && USBD_SUCCESS(hdr.Status))) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, %s, %!STATUS!, USBD_STATUS_%s", 
			ptr04x(fltr.self), urb_function_str(func), irp->IoStatus.Status, 
			get_usbd_status(hdr.Status));

	} else if (func == URB_FUNCTION_SELECT_INTERFACE) {
		post_select_intf(fltr, urb.UrbSelectInterface);
	} else {
		NT_ASSERT(func == URB_FUNCTION_SELECT_CONFIGURATION);
		post_select_cfg(fltr, urb.UrbSelectConfiguration);
	}

	CompleteRequest(irp);
	return st;
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

	if (!fltr.is_hub && get_ioctl(irp) == IOCTL_INTERNAL_USB_SUBMIT_URB) {

		switch (auto &urb = *static_cast<URB*>(URB_FROM_IRP(irp)); urb.UrbHeader.Function) {
		case URB_FUNCTION_SELECT_INTERFACE:
		case URB_FUNCTION_SELECT_CONFIGURATION: 
			return handle_select(fltr, irp, urb);
		}
	}

	return ForwardIrp(fltr, irp);

}
