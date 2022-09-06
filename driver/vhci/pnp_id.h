#pragma once

#include <libdrv\pageable.h>
#include <ntdef.h>

struct _IRP;
struct vdev_t;

#define HWID_ROOT1      L"USBIP\\root1"
#define HWID_ROOT2      L"USBIP\\root2"

#define HWID_EHCI       L"USBIP\\ehci"
#define HWID_XHCI       L"USBIP\\xhci"

// @see usbip_vhci.inf
#define VHUB_PREFIX	L"USB\\ROOT_HUB"
#define VHUB_VID	L"1209"
#define VHUB_PID	L"8250"
#define VHUB_REV	L"0000"

#define VHUB30_PREFIX	L"USB\\ROOT_HUB30"
#define VHUB30_VID	VHUB_VID
#define VHUB30_PID	L"8251"
#define VHUB30_REV      VHUB_REV	

#define HWID_VHUB \
	VHUB_PREFIX \
	L"&VID_" VHUB_VID \
	L"&PID_" VHUB_PID \
	L"&REV_" VHUB_REV

#define HWID_VHUB30 \
	VHUB30_PREFIX \
	L"&VID_" VHUB30_VID \
	L"&PID_" VHUB30_PID \
	L"&REV_" VHUB30_REV

PAGEABLE NTSTATUS pnp_query_id(vdev_t *vdev, _IRP *irp); 
