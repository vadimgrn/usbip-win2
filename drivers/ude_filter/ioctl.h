/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbiodef.h>

namespace usbip
{

// Valid for IRP_MJ_INTERNAL_DEVICE_CONTROL
enum : UINT16 { USBIP_SUBMIT_URB = 0x800 }; // vendor-assigned value, 12 bit

const auto IOCTL_INTERNAL_USBIP_SUBMIT_URB = USB_KERNEL_CTL(USBIP_SUBMIT_URB);

} // namespace usbip

