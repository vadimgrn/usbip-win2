#pragma once

#include <ntddk.h>
#include <wmilib.h>	// required for WMILIB_CONTEXT

#include "devconf.h"
#include "usbdsc.h"

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
	VDEV_VPDO,
	VDEV_SIZE // number of types
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

struct root_dev_t : vdev_t {};
struct cpdo_dev_t : vdev_t {};
struct hpdo_dev_t : vdev_t {};

struct vhci_dev_t : vdev_t
{
	UNICODE_STRING	DevIntfVhci;
	UNICODE_STRING	DevIntfUSBHC;

	WMILIB_CONTEXT	WmiLibInfo;
	USBIP_BUS_WMI_STD_DATA	StdUSBIPBusData;
};

// The device extension for the vpdo.
// That's of the USBIP device which this bus driver enumerates.
struct vpdo_dev_t : vdev_t
{
	int port; // unique port number of the device on hub, [1, vhub_dev_t::NUM_PORTS]
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
	FILE_OBJECT *fo;
	
	unsigned int devid;
	static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

	seqnum_t seqnum;
	seqnum_t seqnum_payload; // *ioctl irp which is wating for read irp for payload transfer

	IO_CSQ read_irp_csq; // waiting for irp from *ioctl
	IRP *read_irp; // from vhci_read, can be only one

	IO_CSQ rx_irps_csq; // waiting for irp from vhci_read
	LIST_ENTRY rx_irps; // from *ioctl
	KSPIN_LOCK rx_irps_lock;

	IO_CSQ tx_irps_csq; // waiting for irp from vhci_write
	LIST_ENTRY tx_irps; // from *ioctl
	KSPIN_LOCK tx_irps_lock;

	LIST_ENTRY rx_canceled_irps; // waiting for irp from vhci_read
	LIST_ENTRY tx_canceled_irps; // waiting for irp from vhci_write
	KSPIN_LOCK canceled_irps_lock;
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

void *GetDeviceProperty(DEVICE_OBJECT *pdo, DEVICE_REGISTRY_PROPERTY prop, NTSTATUS &error, ULONG &ResultLength);

constexpr auto is_fdo(vdev_type_t type)
{
	return type == VDEV_ROOT || type == VDEV_VHCI || type == VDEV_VHUB;
}

vhub_dev_t *vhub_from_vhci(vhci_dev_t *vhci);

inline auto vhub_from_vpdo(vpdo_dev_t *vpdo)
{
	NT_ASSERT(vpdo);
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

seqnum_t next_seqnum(vpdo_dev_t *vpdo);

inline auto as_seqnum(const void *p)
{
	return static_cast<seqnum_t>(reinterpret_cast<uintptr_t>(p));
}

inline auto as_pointer(seqnum_t seqnum)
{
	return reinterpret_cast<void*>(uintptr_t(seqnum));
}

/*
* IoCsqXxx routines use the DriverContext[3] member of the IRP to hold IRP context information. 
* Drivers that use these routines to queue IRPs must leave that member unused.
*/
inline void set_seqnum(IRP *irp, seqnum_t seqnum)
{
	NT_ASSERT(seqnum);
	irp->Tail.Overlay.DriverContext[0] = as_pointer(seqnum);
}

inline auto get_seqnum(IRP *irp)
{
	return as_seqnum(irp->Tail.Overlay.DriverContext[0]);
}

inline void set_seqnum_unlink(IRP *irp, seqnum_t seqnum_unlink)
{
	NT_ASSERT(seqnum_unlink);
	irp->Tail.Overlay.DriverContext[1] = as_pointer(seqnum_unlink);
}

inline auto get_seqnum_unlink(IRP *irp)
{
	return as_seqnum(irp->Tail.Overlay.DriverContext[1]);
}

void enqueue_canceled_irp(vpdo_dev_t *vpdo, IRP *irp);
IRP *dequeue_canceled_irp(vpdo_dev_t *vpdo, LIST_ENTRY *head);