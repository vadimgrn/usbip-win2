#pragma once

#include <guiddef.h>
#ifdef _NTDDK_
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

//
// Define an Interface Guid for bus vhci class.
// This GUID is used to register (IoRegisterDeviceInterface) 
// an instance of an interface so that vhci application 
// can send an ioctl to the bus driver.
//

DEFINE_GUID(GUID_DEVINTERFACE_VHCI_USBIP,
        0xD35F7840, 0x6A0C, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);


//
// Define a Setup Class GUID for USBIP Class. This is same
// as the TOASTSER CLASS guid in the INF files.
//
DEFINE_GUID(GUID_DEVCLASS_USBIP,
        0xB85B7C50, 0x6A01, 0x11d2, 0xB8, 0x41, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{B85B7C50-6A01-11d2-B841-00C04FAD5171}

//
// Define a WMI GUID to get vhci info.
//
DEFINE_GUID(USBIP_BUS_WMI_STD_DATA_GUID, 
        0x0006A660, 0x8F12, 0x11d2, 0xB8, 0x54, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{0006A660-8F12-11d2-B854-00C04FAD5171}

//
// Define a WMI GUID to get USBIP device info.
//
DEFINE_GUID(USBIP_WMI_STD_DATA_GUID, 
        0xBBA21300, 0x6DD3, 0x11d2, 0xB8, 0x44, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);
//{BBA21300-6DD3-11d2-B844-00C04FAD5171}

//
// Define a WMI GUID to represent device arrival notification WMIEvent class.
//
DEFINE_GUID(USBIP_NOTIFY_DEVICE_ARRIVAL_EVENT, 
        0x01CDAFF1, 0xC901, 0x45B4, 0xB3, 0x59, 0xB5, 0x54, 0x27, 0x25, 0xE2, 0x9C);
// {01CDAFF1-C901-45B4-B359-B5542725E29C}

#define USBIP_VHCI_IOCTL(_index_) \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, _index_, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_VHCI_PLUGIN_HARDWARE	USBIP_VHCI_IOCTL(0x0)
#define IOCTL_USBIP_VHCI_UNPLUG_HARDWARE	USBIP_VHCI_IOCTL(0x1)
/* used by attacher */
#define IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE	USBIP_VHCI_IOCTL(0x2)
#define IOCTL_USBIP_VHCI_GET_PORTS_STATUS	USBIP_VHCI_IOCTL(0x3)
#define IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES	USBIP_VHCI_IOCTL(0x4)

#define MAX_VHCI_SERIAL_ID	127

typedef struct _vhci_pluginfo
{
	/* vhci_pluginfo_t structure size */
	unsigned long	size;
	unsigned int	devid;
	signed char	port;
	wchar_t		wserial[MAX_VHCI_SERIAL_ID + 1];
	unsigned char	dscr_dev[18];
	/* variable length. It's a full-length configuration descriptor */
	unsigned char	dscr_conf[9];
} vhci_pluginfo_t, *pvhci_pluginfo_t;

/* usbip-win assumes max port is 127 */
typedef struct _ioctl_usbip_vhci_get_ports_status
{
	/* maximum number of ports */
	unsigned char n_max_ports;
	unsigned char port_status[127];
} ioctl_usbip_vhci_get_ports_status;

typedef struct _ioctl_usbip_vhci_unplug
{
	signed char addr;
	char unused[3];
} ioctl_usbip_vhci_unplug, *pvhci_unpluginfo_t;

typedef struct usbip_imported_device {
	char		port;
	enum usbip_device_status	status;
	unsigned short	vendor;
	unsigned short	product;
	unsigned char	speed;
} ioctl_usbip_vhci_imported_dev, *pioctl_usbip_vhci_imported_dev_t;
