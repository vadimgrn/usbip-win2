/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <wdm.h>
#include <usbiodef.h>

namespace usbip
{

/*
 * Function codes for kernel mode IOCTLs with DeviceType : FILE_DEVICE_USBEX
 * The following codes are valid only if passed as in the icControlCode parameter 
 * for IRP_MJ_INTERNAL_DEVICE_CONTROL.
 */
enum : USHORT { USBEX_SUBMIT_URB = USB_RESERVED_USER_BASE + 13 }; // Function

enum : ULONG { // see IOCTL_INTERNAL_USB_SUBMIT_URB
        IOCTL_INTERNAL_USBEX_SUBMIT_URB = CTL_CODE(FILE_DEVICE_USBEX, USBEX_SUBMIT_URB, METHOD_NEITHER, FILE_ANY_ACCESS)
};

inline auto DeviceIoControlCode(_In_ IRP *irp)
{
        auto stack = IoGetCurrentIrpStackLocation(irp);
        return stack->Parameters.DeviceIoControl.IoControlCode;
}

inline auto urb_from_irp(_In_ IRP *irp)
{
	return static_cast<URB*>(URB_FROM_IRP(irp));
}

} // namespace usbip

