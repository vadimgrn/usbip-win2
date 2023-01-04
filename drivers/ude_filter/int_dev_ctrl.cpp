/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "device.h"
#include "irp.h"

#include <libdrv\remove_lock.h>
#include <libdrv\dbgcommon.h>

#include <usb.h>
#include <usbioctl.h>

namespace
{

inline auto get_ioctl(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return stack->Parameters.DeviceIoControl.IoControlCode;
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
		auto &urb = *static_cast<URB*>(URB_FROM_IRP(irp));
		TraceDbg("%04x, %s", ptr04x(devobj), urb_function_str(urb.UrbHeader.Function));
	}

	return ForwardIrpAsync(fltr.lower, irp);
}
