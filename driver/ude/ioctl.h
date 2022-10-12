/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>
#include <usbioctl.h>

namespace usbip
{

/*
 * Function codes for kernel mode IOCTLs with DeviceType : FILE_DEVICE_USBEX
 * The following codes are valid only if passed as in the icControlCode parameter 
 * for IRP_MJ_INTERNAL_DEVICE_CONTROL.
 */
enum : USHORT { USBEX_SUBMIT_URB = USB_RESERVED_USER_BASE + 13 }; // Function

enum : ULONG { // see IOCTL_INTERNAL_USB_SUBMIT_URB
        IOCTL_INTERNAL_USBEX_SELECT = CTL_CODE(FILE_DEVICE_USBEX, USBEX_SUBMIT_URB, METHOD_NEITHER, FILE_ANY_ACCESS)
};

inline auto DeviceIoControlCode(_In_ _IO_STACK_LOCATION *stack)
{
	return stack->Parameters.DeviceIoControl.IoControlCode;
}

inline auto DeviceIoControlCode(_In_ IRP *irp)
{
        auto stack = IoGetCurrentIrpStackLocation(irp);
        return DeviceIoControlCode(stack);
}

inline auto has_urb(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return  stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL && 
		DeviceIoControlCode(stack) == IOCTL_INTERNAL_USB_SUBMIT_URB;
}

inline auto urb_from_irp(_In_ IRP *irp)
{
	return static_cast<URB*>(URB_FROM_IRP(irp));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto try_get_urb(_In_ WDFREQUEST request)
{
	auto irp = WdfRequestWdmGetIrp(request);
	return has_urb(irp) ? urb_from_irp(irp) : nullptr;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_urb(_In_ WDFREQUEST request)
{
	auto irp = WdfRequestWdmGetIrp(request);
	NT_ASSERT(has_urb(irp));
	return *urb_from_irp(irp);
}

} // namespace usbip

