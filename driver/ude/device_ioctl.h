/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <usbiodef.h>

namespace usbip
{

const auto IOCTL_INTERNAL_USBEX_SUBMIT_URB = // see IOCTL_INTERNAL_USB_SUBMIT_URB
        CTL_CODE(FILE_DEVICE_USBEX, USB_RESERVED_USER_BASE + 13, METHOD_NEITHER, FILE_ANY_ACCESS);

inline auto DeviceIoControlCode(_In_ IRP *irp)
{
        NT_ASSERT(irp);
        auto stack = IoGetCurrentIrpStackLocation(irp);
        return stack->Parameters.DeviceIoControl.IoControlCode;
}

} // namespace usbip


namespace usbip::device
{

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS select_configuration(_In_ UDECXUSBDEVICE dev, _In_ WDFREQUEST request, _In_ UCHAR ConfigurationValue);

_Function_class_(EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI internal_device_control(
        _In_ WDFQUEUE Queue,
        _In_ WDFREQUEST Request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode);


} // namespace usbip::device
