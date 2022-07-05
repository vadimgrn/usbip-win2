#pragma once

#include "pageable.h"
#include "dev.h"

#define HWID_ROOT	L"USBIPWIN\\root"
#define HWID_VHCI	L"USBIPWIN\\vhci"

#define VHUB_PREFIX	L"USB\\ROOT_HUB"
#define VHUB_VID	L"1209"
#define VHUB_PID	L"8250"
#define VHUB_REV	L"0000"

#define HWID_VHUB \
	VHUB_PREFIX \
	L"&VID_" VHUB_VID \
	L"&PID_" VHUB_PID \
	L"&REV_" VHUB_REV


inline LONG VpdoCount;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void set_state(vdev_t &vdev, pnp_state state);

inline void set_previous_pnp_state(vdev_t &vdev)
{
	vdev.PnPState = vdev.PreviousPnPState;
}

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_PNP)
extern "C" PAGEABLE NTSTATUS vhci_pnp(__in PDEVICE_OBJECT devobj, __in IRP *irp);
