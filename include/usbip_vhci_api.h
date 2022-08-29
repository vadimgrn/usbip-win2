#pragma once

#include <guiddef.h>

#ifdef _NTDDK_
  #include <ntddk.h>
#else
  #include <winioctl.h>
#endif

#include <usbspec.h>

#include "ch9.h"
#include "usbip_api_consts.h"
#include "usbip_proto.h"

enum vdev_usb_t { VDEV_USB2, VDEV_USB3, VDEV_USB_CNT };
enum { VHUB_NUM_PORTS = 15, USBIP_TOTAL_PORTS = int(VHUB_NUM_PORTS)*VDEV_USB_CNT };

constexpr auto get_rhport(int port) // [1..USBIP_TOTAL_PORTS]
{
        return --port % VHUB_NUM_PORTS + 1;
}
static_assert(get_rhport(1) == 1);
static_assert(get_rhport(VHUB_NUM_PORTS) == VHUB_NUM_PORTS);
static_assert(get_rhport(VHUB_NUM_PORTS + 1) == 1);
static_assert(get_rhport(2*VHUB_NUM_PORTS) == VHUB_NUM_PORTS);

constexpr auto get_vdev_usb(int port) // [1..USBIP_TOTAL_PORTS]
{
        return vdev_usb_t(--port/VHUB_NUM_PORTS);
}
static_assert(get_vdev_usb(1) == VDEV_USB2);
static_assert(get_vdev_usb(VHUB_NUM_PORTS) == VDEV_USB2);
static_assert(get_vdev_usb(VHUB_NUM_PORTS + 1) == VDEV_USB3);
static_assert(get_vdev_usb(2*VHUB_NUM_PORTS) == VDEV_USB3);

DEFINE_GUID(GUID_DEVINTERFACE_EHCI_USBIP,
        0xD35F7840, 0x6A0C, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

DEFINE_GUID(GUID_DEVINTERFACE_XHCI_USBIP,
        0xC1B20918, 0x5628, 0x42F8, 0xA6, 0xD4, 0xA9, 0x2C, 0x8C, 0xCE, 0xB1, 0x8F);

constexpr auto& usbip_guid(vdev_usb_t version)
{
        return version == VDEV_USB3 ? GUID_DEVINTERFACE_XHCI_USBIP : GUID_DEVINTERFACE_EHCI_USBIP;
}

DEFINE_GUID(USBIP_BUS_WMI_STD_DATA_GUID, 
        0x0006A660, 0x8F12, 0x11d2, 0xB8, 0x54, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

#define USBIP_VHCI_IOCTL(_index_) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, _index_, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE	USBIP_VHCI_IOCTL(0)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE	USBIP_VHCI_IOCTL(1)
// used by usbip_vhci.c
#define IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES	USBIP_VHCI_IOCTL(2)

struct ioctl_usbip_vhci_plugin
{
        int port; // OUT, must be the first member; port# if > 0 else err_t
        char busid[USBIP_BUS_ID_SIZE];
        char service[32]; // NI_MAXSERV
        char host[1025];  // NI_MAXHOST in ws2def.h
        char serial[255];
};

struct ioctl_usbip_vhci_imported_dev : ioctl_usbip_vhci_plugin
{
        usbip_device_status status;
        unsigned short vendor;
        unsigned short product;
        UINT32 devid;
        usb_device_speed speed;
};

struct ioctl_usbip_vhci_unplug
{
        int port;
};
