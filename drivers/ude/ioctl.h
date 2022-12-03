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
enum : USHORT {  // Function
	USBEX_BASE = USB_RESERVED_USER_BASE,
	USBEX_CFG_INIT = USBEX_BASE + 5, // UdecxEndpointsConfigureTypeDeviceInitialize
	USBEX_CFG_CHANGE = USBEX_BASE + 13, // UdecxEndpointsConfigureTypeDeviceConfigurationChange, UdecxEndpointsConfigureTypeInterfaceSettingChange
};

enum : ULONG { // see IOCTL_INTERNAL_USB_SUBMIT_URB
        IOCTL_INTERNAL_USBEX_CFG_INIT   = CTL_CODE(FILE_DEVICE_USBEX, USBEX_CFG_INIT,   METHOD_NEITHER, FILE_ANY_ACCESS),
	IOCTL_INTERNAL_USBEX_CFG_CHANGE = CTL_CODE(FILE_DEVICE_USBEX, USBEX_CFG_CHANGE, METHOD_NEITHER, FILE_ANY_ACCESS)
};
static_assert(IOCTL_INTERNAL_USBEX_CFG_INIT   == 0x491017);
static_assert(IOCTL_INTERNAL_USBEX_CFG_CHANGE == 0x491037);

inline auto DeviceIoControlCode(_In_ _IO_STACK_LOCATION *stack)
{
	return stack->Parameters.DeviceIoControl.IoControlCode;
}

inline auto DeviceIoControlCode(_In_ IRP *irp)
{
        auto stack = IoGetCurrentIrpStackLocation(irp);
        return DeviceIoControlCode(stack);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ IRP *irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	return  stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL && 
		DeviceIoControlCode(stack) == IOCTL_INTERNAL_USB_SUBMIT_URB;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto has_urb(_In_ WDFREQUEST request)
{
	auto irp = WdfRequestWdmGetIrp(request);
	return has_urb(irp);
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

