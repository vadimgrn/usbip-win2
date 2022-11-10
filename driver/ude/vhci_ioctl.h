/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv/wdf_cpp.h>

struct _UDECXUSBDEVICE_INIT;
struct usbip_usb_device;

namespace usbip 
{

struct device_ctx_ext;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS add_descriptors(_In_ _UDECXUSBDEVICE_INIT *init, _In_ device_ctx_ext &dev, _In_ const usbip_usb_device &udev);

} // namespace usbip 


namespace usbip::vhci
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_default_queue(_In_ WDFDEVICE vhci);

} // namespace usbip::vhci
