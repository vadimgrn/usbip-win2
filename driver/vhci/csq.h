#pragma once

#include "pageable.h"
#include "usbip_proto.h"

#include <wdm.h>
#include <usb.h>

struct vpdo_dev_t;
PAGEABLE NTSTATUS init_queue(vpdo_dev_t &vpdo);

inline bool is_initialized(const IO_CSQ &csq)
{
        return csq.CsqAcquireLock;
}

struct peek_context
{
	union {
		seqnum_t seqnum;
		USBD_PIPE_HANDLE handle;
	};
	bool use_seqnum;
};

void enqueue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ IRP *irp);
IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ seqnum_t seqnum);

enum irp_status_t { ST_NONE, ST_SEND_COMPLETE, ST_RECV_COMPLETE, ST_IRP_CANCELED };
void set_context(IRP *irp, seqnum_t seqnum, irp_status_t status, bool clear_pipe_handle);

constexpr auto make_peek_context(seqnum_t seqnum)
{
	return peek_context{{seqnum}, true};
}

inline auto make_peek_context(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	peek_context ctx{};
	ctx.handle = handle;
	return ctx;
}

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
	irp->Tail.Overlay.DriverContext[0] = as_pointer(seqnum);
}

inline auto get_seqnum(IRP *irp)
{
	return as_seqnum(irp->Tail.Overlay.DriverContext[0]);
}

inline auto get_status(IRP *irp)
{
	return reinterpret_cast<LONG*>(irp->Tail.Overlay.DriverContext + 1);
}

inline void set_pipe_handle(IRP *irp, USBD_PIPE_HANDLE PipeHandle)
{
	irp->Tail.Overlay.DriverContext[2] = PipeHandle;
}

inline auto get_pipe_handle(IRP *irp)
{
	return static_cast<USBD_PIPE_HANDLE>(irp->Tail.Overlay.DriverContext[2]);
}
