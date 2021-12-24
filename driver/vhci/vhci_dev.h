#pragma once

#include <ntddk.h>
#include <wmilib.h>	// required for WMILIB_CONTEXT

#include "vhci_devconf.h"

extern LPCWSTR devcodes[];

// These are the states a vpdo or vhub transition upon
// receiving a specific PnP Irp. Refer to the PnP Device States
// diagram in DDK documentation for better understanding.
typedef enum _DEVICE_PNP_STATE {
	NotStarted = 0,		// Not started yet
	Started,		// Device has received the START_DEVICE IRP
	StopPending,		// Device has received the QUERY_STOP IRP
	Stopped,		// Device has received the STOP_DEVICE IRP
	RemovePending,		// Device has received the QUERY_REMOVE IRP
	SurpriseRemovePending,	// Device has received the SURPRISE_REMOVE IRP
	Deleted,		// Device has received the REMOVE_DEVICE IRP
	UnKnown			// Unknown state
} DEVICE_PNP_STATE;

// Structure for reporting data to WMI
typedef struct _USBIP_BUS_WMI_STD_DATA
{
	// The error Count
	UINT32   ErrorCount;
} USBIP_BUS_WMI_STD_DATA, *PUSBIP_BUS_WMI_STD_DATA;

typedef enum {
	VDEV_ROOT,
	VDEV_CPDO,
	VDEV_VHCI,
	VDEV_HPDO,
	VDEV_VHUB,
	VDEV_VPDO
} vdev_type_t;

// A common header for the device extensions of the vhub and vpdo
typedef struct _vdev {
	// A back pointer to the device object for which this is the extension
	PDEVICE_OBJECT	Self;

	vdev_type_t		type;
	// reference count for maintaining vdev validity
	LONG	n_refs;

	// We track the state of the device with every PnP Irp
	// that affects the device through these two variables.
	DEVICE_PNP_STATE	DevicePnPState;
	DEVICE_PNP_STATE	PreviousPnPState;

	// Stores the current system power state
	SYSTEM_POWER_STATE	SystemPowerState;

	// Stores current device power state
	DEVICE_POWER_STATE	DevicePowerState;


	// root and vhci have cpdo and hpdo each
	struct _vdev	*child_pdo, *parent, *fdo;
	PDEVICE_OBJECT	pdo;
	PDEVICE_OBJECT	devobj_lower;
} vdev_t, *pvdev_t;

struct urb_req;
struct _cpdo;
struct _vhub;
struct _hpdo;

typedef struct
{
	vdev_t	common;
} root_dev_t, *proot_dev_t;

typedef struct _cpdo
{
	vdev_t	common;
} cpdo_dev_t, *pcpdo_dev_t;

typedef struct
{
	vdev_t	common;

	UNICODE_STRING	DevIntfVhci;
	UNICODE_STRING	DevIntfUSBHC;

	// WMI Information
	WMILIB_CONTEXT	WmiLibInfo;

	USBIP_BUS_WMI_STD_DATA	StdUSBIPBusData;
} vhci_dev_t, *pvhci_dev_t;

typedef struct _hpdo
{
	vdev_t	common;
} hpdo_dev_t, *phpdo_dev_t;

// The device extension of the vhub.  From whence vpdo's are born.
typedef struct _vhub
{
	vdev_t	common;

	// List of vpdo's created so far
	LIST_ENTRY	head_vpdo;

	ULONG		n_max_ports;

	// the number of current vpdo's
	ULONG		n_vpdos;
	ULONG		n_vpdos_plugged;

	// A synchronization for access to the device extension.
	FAST_MUTEX	Mutex;

	// The number of IRPs sent from the bus to the underlying device object
	LONG		OutstandingIO; // Biased to 1

	UNICODE_STRING	DevIntfRootHub;

	// On remove device plug & play request we must wait until all outstanding
	// requests have been completed before we can actually delete the device
	// object. This event is when the Outstanding IO count goes to zero
	KEVENT		RemoveEvent;
} vhub_dev_t, *pvhub_dev_t;

// The device extension for the vpdo.
// That's of the USBIP device which this bus driver enumerates.
typedef struct
{
	vdev_t	common;

	USHORT	vendor, product, revision;
	UCHAR	usbclass, subclass, protocol, inum;

	PWSTR	serial; // device serial number
	PWSTR	serial_usr; // user-defined serial number

	ULONG	port; // unique port number of the device on the bus

	// Link point to hold all the vpdos for a single bus together
	LIST_ENTRY	Link;

	// set to TRUE when the vpdo is exposed via PlugIn IOCTL,
	// and set to FALSE when a UnPlug IOCTL is received.
	BOOLEAN		plugged;

	enum usb_device_speed speed; 
	UCHAR	num_configurations; // Number of Possible Configurations

	// a pending irp when no urb is requested
	PIRP	pending_read_irp;
	// a partially transferred urb_req
	struct urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG	len_sent_partial;
	// all urb_req's. This list will be used for clear or cancellation.
	LIST_ENTRY	head_urbr;
	// pending urb_req's which are not transferred yet
	LIST_ENTRY	head_urbr_pending;
	// urb_req's which had been sent and have waited for response
	LIST_ENTRY	head_urbr_sent;
	KSPIN_LOCK	lock_urbr;
	PFILE_OBJECT	fo;
	unsigned int	devid;
	unsigned long	seq_num;
	USB_DEVICE_DESCRIPTOR *dsc_dev;
	USB_CONFIGURATION_DESCRIPTOR *actconfig; // NULL if unconfigured
	UNICODE_STRING	usb_dev_interface;
	UCHAR	current_intf_num, current_intf_alt;
} vpdo_dev_t, *pvpdo_dev_t;

PDEVICE_OBJECT vdev_create(PDRIVER_OBJECT drvobj, vdev_type_t type);

__inline void vdev_add_ref(vdev_t *vdev)
{
	InterlockedIncrement(&vdev->n_refs);
}

__inline void vdev_del_ref(vdev_t *vdev)
{
	InterlockedDecrement(&vdev->n_refs);
}

pvpdo_dev_t vhub_find_vpdo(pvhub_dev_t vhub, unsigned port);

void vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

LPWSTR get_device_prop(PDEVICE_OBJECT pdo, DEVICE_REGISTRY_PROPERTY prop, PULONG plen);

#define TO_DEVOBJ(vdev)		((vdev)->common.Self)

__inline bool is_fdo(vdev_type_t type)
{
	return type == VDEV_ROOT || type == VDEV_VHCI || type == VDEV_VHUB;
}

__inline vhub_dev_t *vhub_from_vhci(vhci_dev_t *vhci)
{	
	struct _vdev *child_pdo = vhci->common.child_pdo;
	return child_pdo ? (vhub_dev_t*)child_pdo->fdo : NULL;
}

__inline vhub_dev_t *vhub_from_vpdo(vpdo_dev_t *vpdo)
{
	return (vhub_dev_t*)(vpdo->common.parent);
}

__inline vdev_t *devobj_to_vdev(DEVICE_OBJECT *devobj) 
{ 
	NT_ASSERT(devobj);
	return devobj->DeviceExtension; 
}

cpdo_dev_t *devobj_to_cpdo_or_null(DEVICE_OBJECT *devobj);
vhci_dev_t *devobj_to_vhci_or_null(DEVICE_OBJECT *devobj);
hpdo_dev_t *devobj_to_hpdo_or_null(DEVICE_OBJECT *devobj);
vhub_dev_t *devobj_to_vhub_or_null(DEVICE_OBJECT *devobj);
vpdo_dev_t *devobj_to_vpdo_or_null(DEVICE_OBJECT *devobj);
