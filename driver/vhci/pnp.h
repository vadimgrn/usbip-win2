#pragma once

#include "pageable.h"
#include "dev.h"

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
extern "C" PAGEABLE NTSTATUS vhci_pnp(_In_ PDEVICE_OBJECT devobj, _In_ IRP *irp);
