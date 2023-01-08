/*
* Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>
#include <usb.h>
#include <usbioctl.h>

namespace libdrv
{

inline auto& DeviceIoControlCode(_In_ _IO_STACK_LOCATION *stack)
{
	return stack->Parameters.DeviceIoControl.IoControlCode;
}

inline auto& DeviceIoControlCode(_In_ IRP *irp, _In_ IO_STACK_LOCATION *stack = nullptr)
{
	if (!stack) {
		stack = IoGetCurrentIrpStackLocation(irp);
	}

	return DeviceIoControlCode(stack);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ IRP *irp, _In_ IO_STACK_LOCATION *stack = nullptr)
{
	if (!stack) {
		stack = IoGetCurrentIrpStackLocation(irp);
	}

	return  stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL && 
		DeviceIoControlCode(stack) == IOCTL_INTERNAL_USB_SUBMIT_URB;
}

inline auto urb_from_irp(_In_ IRP *irp)
{
	return static_cast<URB*>(URB_FROM_IRP(irp));
}

} // namespace libdrv
