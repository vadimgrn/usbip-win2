#pragma once

#include <guiddef.h>

#ifdef _KERNEL_MODE
  #include <ntddk.h>
#else
  #include <winioctl.h>
#endif

#include <usbspec.h>

#include "ch9.h"
#include "usbip_api_consts.h"
#include "usbip_proto.h"

enum hci_version { HCI_USB2, HCI_USB3 };
inline const hci_version vhci_list[] { HCI_USB2, HCI_USB3 };

enum { 
        VHUB_NUM_PORTS = 0xF, 
        USBIP_TOTAL_PORTS = ARRAYSIZE(vhci_list)*VHUB_NUM_PORTS // max 127 devices on usb bus
};

constexpr auto is_valid_rhport(int port) // root hub port
{
        return port > 0 && port <= VHUB_NUM_PORTS;
}

constexpr auto is_valid_vport(int port) // virtual port
{
        return port > 0 && port <= USBIP_TOTAL_PORTS;
}


constexpr auto get_hci_version(int vport) // [1..USBIP_TOTAL_PORTS]
{
        return hci_version(--vport/VHUB_NUM_PORTS);
}
static_assert(get_hci_version(1) == HCI_USB2);
static_assert(get_hci_version(VHUB_NUM_PORTS) == HCI_USB2);
static_assert(get_hci_version(VHUB_NUM_PORTS + 1) == HCI_USB3);
static_assert(get_hci_version(USBIP_TOTAL_PORTS) == HCI_USB3);


constexpr auto get_rhport(int vport) // [1..USBIP_TOTAL_PORTS]
{
        return --vport % VHUB_NUM_PORTS + 1;
}
static_assert(get_rhport(1) == 1);
static_assert(get_rhport(VHUB_NUM_PORTS) == VHUB_NUM_PORTS);
static_assert(get_rhport(VHUB_NUM_PORTS + 1) == 1);
static_assert(get_rhport(USBIP_TOTAL_PORTS) == VHUB_NUM_PORTS);


constexpr auto make_vport(hci_version version, int rhport) // [1..VHUB_NUM_PORTS]
{
        return int(VHUB_NUM_PORTS)*version + rhport;
}
static_assert(make_vport(HCI_USB3, VHUB_NUM_PORTS) == USBIP_TOTAL_PORTS);

static_assert(get_hci_version(make_vport(HCI_USB2, VHUB_NUM_PORTS)) == HCI_USB2);
static_assert(get_hci_version(make_vport(HCI_USB3, VHUB_NUM_PORTS)) == HCI_USB3);

static_assert(get_rhport(make_vport(HCI_USB2, VHUB_NUM_PORTS)) == VHUB_NUM_PORTS);
static_assert(get_rhport(make_vport(HCI_USB3, VHUB_NUM_PORTS)) == VHUB_NUM_PORTS);


DEFINE_GUID(GUID_DEVINTERFACE_EHCI_USBIP,
        0xB8B60941, 0xCACB, 0x454A, 0xA8, 0xD1, 0x35, 0xAC, 0xB8, 0xFA, 0x1F, 0x1E);

DEFINE_GUID(GUID_DEVINTERFACE_XHCI_USBIP,
        0xC1B20918, 0x5628, 0x42F8, 0xA6, 0xD4, 0xA9, 0x2C, 0x8C, 0xCE, 0xB1, 0x8F);

constexpr auto& vhci_guid(hci_version version)
{
        return version == HCI_USB3 ? GUID_DEVINTERFACE_XHCI_USBIP : GUID_DEVINTERFACE_EHCI_USBIP;
}

DEFINE_GUID(USBIP_BUS_WMI_STD_DATA_GUID,
        0xCF26E276, 0x6C60, 0x4442, 0x8B, 0x58, 0x93, 0xAD, 0xA6, 0x69, 0x39, 0xB3);

constexpr auto USBIP_VHCI_IOCTL(int idx)
{
        return CTL_CODE(FILE_DEVICE_BUS_EXTENDER, idx, METHOD_BUFFERED, FILE_READ_DATA);
}

enum {
        IOCTL_USBIP_VHCI_PLUGIN_HARDWARE      = USBIP_VHCI_IOCTL(0),
        IOCTL_USBIP_VHCI_UNPLUG_HARDWARE      = USBIP_VHCI_IOCTL(1),
        IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES = USBIP_VHCI_IOCTL(2),
};

struct ioctl_usbip_vhci_plugin
{
        int port; // OUT, must be the first member; [1..USBIP_TOTAL_PORTS] in (port & 0xFFFF) or see make_error()
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
        int port; // [1..USBIP_TOTAL_PORTS] or all ports if <= 0
};
