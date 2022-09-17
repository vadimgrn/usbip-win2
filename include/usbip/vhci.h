#pragma once

#include <guiddef.h>

#ifdef _KERNEL_MODE
  #include <ntddk.h>
#else
  #include <windows.h>
  #include <winioctl.h>
#endif

#include <usbspec.h>

#include "ch9.h"
#include "consts.h"
#include "proto.h"

enum hci_version { HCI_USB2, HCI_USB3 };

enum { 
        VHUB_NUM_PORTS = 30, // see ch11.h, USB_MAXCHILDREN
        USBIP_TOTAL_PORTS = 2*VHUB_NUM_PORTS
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

DEFINE_GUID(GUID_DEVINTERFACE_USBIP_HOST_CONTROLLER,
        0xB4030C06, 0xDC5F, 0x4FCC, 0x87, 0xEB, 0xE5, 0x51, 0x5A, 0x09, 0x35, 0xC0);

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
