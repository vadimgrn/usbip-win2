/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "int_dev_ctrl.h"
#include "trace.h"
#include "int_dev_ctrl.tmh"

#include "irp.h"
#include <libdrv\remove_lock.h>

#include <usb.h>
#include <usbioctl.h>

namespace
{

inline auto get_ioctl(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return stack->Parameters.DeviceIoControl.IoControlCode;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void select_configuration(_In_ DEVICE_OBJECT *devobj, _In_ const _URB_SELECT_CONFIGURATION &r)
{
	auto cd = r.ConfigurationDescriptor; // nullptr if unconfigured
	UCHAR value = cd ? cd->bConfigurationValue : 0;

	TraceDbg("dev %04x, ConfigurationHandle %04x, bConfigurationValue %d", 
		  ptr04x(devobj), ptr04x(r.ConfigurationHandle), value);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
void select_iterface(_In_ DEVICE_OBJECT *devobj, _In_ const _URB_SELECT_INTERFACE &r)
{
	auto &iface = r.Interface;
	TraceDbg("dev %04x, ConfigurationHandle %04x, InterfaceNumber %d, AlternateSetting %d", 
		  ptr04x(devobj), ptr04x(r.ConfigurationHandle), iface.InterfaceNumber, iface.AlternateSetting);
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
			select_iterface(devobj, urb.UrbSelectInterface);
			break;
		case URB_FUNCTION_SELECT_CONFIGURATION:
			select_configuration(devobj, urb.UrbSelectConfiguration);
			break;
		}
	}

	return ForwardIrp(fltr, irp);
}
