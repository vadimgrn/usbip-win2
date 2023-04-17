/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

#include <usb.h>
#include <usbbusif.h>

namespace usbip
{

struct filter_ext;

struct usbdi_controller_type
{
        ULONG HcdiOptionFlags;
        USHORT PciVendorId;
        USHORT PciDeviceId;
        UCHAR PciClass;
        UCHAR PciSubClass;
        UCHAR PciRevisionId;
        UCHAR PciProgIf;
};

struct usbdi_version
{
        USBD_VERSION_INFORMATION VersionInformation;
        ULONG HcdCapabilities;
};

struct usbdi_bus_info
{
        _USB_BUS_INFORMATION_LEVEL_1 level_1;
        WCHAR data[MAXIMUM_FILENAME_LENGTH]; // rest of level_1.ControllerNameUnicodeString
};

struct usbdi_all
{
        usbdi_version version;
        usbdi_controller_type controller_type;
        bool IsDeviceHighSpeed;
        usbdi_bus_info bus_info;
};

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void query_interface(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &v3);

} // namespace usbip
