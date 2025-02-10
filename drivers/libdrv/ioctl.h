/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <usbioctl.h>

namespace libdrv
{

inline auto& DeviceIoControlCode(_In_ _IO_STACK_LOCATION *stack)
{
	return stack->Parameters.DeviceIoControl.IoControlCode;
}

inline auto& DeviceIoControlCode(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return DeviceIoControlCode(stack);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ IO_STACK_LOCATION *stack)
{
	return  stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL && 
		DeviceIoControlCode(stack) == IOCTL_INTERNAL_USB_SUBMIT_URB;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return has_urb(stack);
}

inline auto urb_from_irp(_In_ IRP *irp)
{
	return static_cast<URB*>(URB_FROM_IRP(irp));
}

} // namespace libdrv
