#pragma once

#include "pageable.h"
#include "usbip_proto.h"

#include <wdm.h>
#include <usb.h>

struct vdev_t;
struct vpdo_dev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_pass_down(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_send_synchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_pass_down_or_complete(_In_ vdev_t *vdev, _In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status = STATUS_SUCCESS);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequestAsIs(_In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
void complete_as_canceled(_In_ IRP *irp);

inline auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

inline auto get_irp(_In_ LIST_ENTRY *entry)
{
	return CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
}

/*
 * IoCsqXxx routines use the DriverContext[3] member of the IRP to hold IRP context information. 
 * Drivers that use these routines to queue IRPs must leave that member unused.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_seqnum(_In_ IRP *irp)
{
	NT_ASSERT(irp);

	auto ptr = irp->Tail.Overlay.DriverContext;
	static_assert(sizeof(*ptr) == 2*sizeof(seqnum_t));

	return *reinterpret_cast<seqnum_t*>(ptr); // low word of DriverContext[0]
}

enum irp_status_t { ST_NONE, ST_SEND_COMPLETE, ST_RECV_COMPLETE, ST_IRP_CANCELED, ST_IRP_NULL };

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto get_status(_In_ IRP *irp)
{
	NT_ASSERT(irp);
	static_assert(sizeof(LONG) == sizeof(seqnum_t));
	return reinterpret_cast<LONG*>(irp->Tail.Overlay.DriverContext) + 1; // high word of DriverContext[0]
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_pipe_handle(_In_ IRP *irp)
{
	NT_ASSERT(irp);
	return *static_cast<USBD_PIPE_HANDLE*>(irp->Tail.Overlay.DriverContext + 1);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto atomic_set_status(_In_ IRP *irp, _In_ irp_status_t status)
{
	NT_ASSERT(status != ST_NONE);
	NT_ASSERT(status != ST_IRP_NULL);
	return InterlockedCompareExchange(get_status(irp), status, ST_NONE);
}
