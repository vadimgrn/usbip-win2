#pragma once

#include <ntddk.h>
#include <wmilib.h>	// required for WMILIB_CONTEXT

#include "devconf.h"
#include "usbdsc.h"

struct urb_req;

// These are the states a vpdo or vhub transition upon
// receiving a specific PnP Irp. Refer to the PnP Device States
// diagram in DDK documentation for better understanding.
enum class pnp_state 
{
	NotStarted,
	Started,		// START_DEVICE
	StopPending,		// QUERY_STOP
	Stopped,		// STOP_DEVICE
	RemovePending,		// QUERY_REMOVE
	SurpriseRemovePending,	// SURPRISE_REMOVE
	Removed			// REMOVE_DEVICE
};

// Structure for reporting data to WMI
struct USBIP_BUS_WMI_STD_DATA
{
	UINT32 ErrorCount;
} ;

enum vdev_type_t {
	VDEV_ROOT,
	VDEV_CPDO,
	VDEV_VHCI,
	VDEV_HPDO,
	VDEV_VHUB,
	VDEV_VPDO
};

// A common header for the device extensions of the vhub and vpdo
struct vdev_t 
{
	// A back pointer to the device object for which this is the extension
	DEVICE_OBJECT *Self;

	vdev_type_t type;

	// We track the state of the device with every PnP Irp
	// that affects the device through these two variables.
	pnp_state PnPState;
	pnp_state PreviousPnPState;

	// Stores the current system power state
	SYSTEM_POWER_STATE SystemPowerState;

	// Stores current device power state
	DEVICE_POWER_STATE DevicePowerState;


	// root and vhci have cpdo and hpdo each
	vdev_t *child_pdo;
	vdev_t *parent;
	vdev_t *fdo;

	DEVICE_OBJECT *pdo;
	DEVICE_OBJECT *devobj_lower;
};

struct root_dev_t : vdev_t
{
};

struct cpdo_dev_t : vdev_t
{
};

struct vhci_dev_t : vdev_t
{
	UNICODE_STRING	DevIntfVhci;
	UNICODE_STRING	DevIntfUSBHC;

	WMILIB_CONTEXT	WmiLibInfo;
	USBIP_BUS_WMI_STD_DATA	StdUSBIPBusData;
};

struct hpdo_dev_t : vdev_t
{
};

// The device extension for the vpdo.
// That's of the USBIP device which this bus driver enumerates.
struct vpdo_dev_t : vdev_t
{
	int port; // unique port number of the device on the bus, [1, vhub_dev_t::NUM_PORTS]
	volatile bool unplugged; // see IOCTL_USBIP_VHCI_UNPLUG_HARDWARE

	USB_DEVICE_DESCRIPTOR descriptor;

	usb_device_speed speed; // corresponding speed for descriptor.bcdUSB 

	// use instead of corresponding members of device descriptor
	UCHAR bDeviceClass;
	UCHAR bDeviceSubClass;
	UCHAR bDeviceProtocol;

	PWSTR Manufacturer; // for descriptor.iManufacturer
	PWSTR Product; // for descriptor.iProduct
	PWSTR SerialNumber; // for descriptor.iSerialNumber
	PWSTR SerialNumberUser; // user-defined serial number

	USB_CONFIGURATION_DESCRIPTOR *actconfig; // NULL if unconfigured

	UCHAR current_intf_num;
	UCHAR current_intf_alt;
	ULONG current_frame_number;

	UNICODE_STRING usb_dev_interface;

	// a pending irp when no urb is requested
	IRP *pending_read_irp;
	// a partially transferred urb_req
	urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG len_sent_partial;
	// all urb_req's. This list will be used for clear or cancellation.
	LIST_ENTRY head_urbr;
	// pending urb_req's which are not transferred yet
	LIST_ENTRY head_urbr_pending;
	// urb_req's which had been sent and have waited for response
	LIST_ENTRY head_urbr_sent;
	KSPIN_LOCK lock_urbr;
	FILE_OBJECT *fo;
	
	unsigned int devid;
	static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

	unsigned long seqnum; // the most significant bit is reserved and must be zero
	static_assert(sizeof(seqnum) >= sizeof(usbip_header_basic::seqnum));
};

// The device extension of the vhub.  From whence vpdo's are born.
struct vhub_dev_t : vdev_t
{
	FAST_MUTEX Mutex;

	enum { NUM_PORTS = 8 }; // see USB_SS_MAXPORTS
	vpdo_dev_t *vpdo[NUM_PORTS];

	UNICODE_STRING DevIntfRootHub;
};

extern "C" PDEVICE_OBJECT vdev_create(PDRIVER_OBJECT drvobj, vdev_type_t type);

LPWSTR get_device_prop(DEVICE_OBJECT *pdo, DEVICE_REGISTRY_PROPERTY prop, ULONG *plen);

constexpr auto is_fdo(vdev_type_t type)
{
	return type == VDEV_ROOT || type == VDEV_VHCI || type == VDEV_VHUB;
}

inline auto vhub_from_vhci(vhci_dev_t *vhci)
{	
	auto child_pdo = vhci->child_pdo;
	return child_pdo ? reinterpret_cast<vhub_dev_t*>(child_pdo->fdo) : nullptr;
}

inline auto vhub_from_vpdo(vpdo_dev_t *vpdo)
{
	return reinterpret_cast<vhub_dev_t*>(vpdo->parent);
}

inline auto to_vdev(DEVICE_OBJECT *devobj)
{ 
	NT_ASSERT(devobj);
	return static_cast<vdev_t*>(devobj->DeviceExtension); 
}

cpdo_dev_t *to_cpdo_or_null(DEVICE_OBJECT *devobj);
vhci_dev_t *to_vhci_or_null(DEVICE_OBJECT *devobj);
hpdo_dev_t *to_hpdo_or_null(DEVICE_OBJECT *devobj);
vhub_dev_t *to_vhub_or_null(DEVICE_OBJECT *devobj);
vpdo_dev_t *to_vpdo_or_null(DEVICE_OBJECT *devobj);

/*
 * See: attacher_xfer.cpp 
 */
inline auto next_seqnum(vpdo_dev_t &vpdo)
{
	static_assert(sizeof(usbip_header_basic::seqnum) == sizeof(UINT32));

	auto &val = vpdo.seqnum;

	if (++val & 0x8000'0000) { // the most significant bit is set
		val = 1;
	}

	NT_ASSERT(val);
	return val;
}