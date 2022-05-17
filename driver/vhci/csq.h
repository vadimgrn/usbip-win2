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

constexpr void *InsertTail() { return nullptr; }
constexpr void *InsertHead() { return InsertTail; }

struct peek_context
{
	bool use_seqnum;
	union {
		seqnum_t seqnum;
		USBD_PIPE_HANDLE handle;
	};
};

constexpr auto make_peek_context(seqnum_t seqnum)
{
	return peek_context{true, {seqnum}};
}

inline auto make_peek_context(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	peek_context ctx{false};
	ctx.handle = handle;
	return ctx;
}

void enqueue_unlink_irp(vpdo_dev_t *vpdo, IRP *irp);
IRP *dequeue_unlink_irp(vpdo_dev_t *vpdo, seqnum_t seqnum_unlink = 0);

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

inline void set_seqnum_unlink(IRP *irp, seqnum_t seqnum_unlink)
{
	irp->Tail.Overlay.DriverContext[1] = as_pointer(seqnum_unlink);
}

inline auto get_seqnum_unlink(IRP *irp)
{
	return as_seqnum(irp->Tail.Overlay.DriverContext[1]);
}

inline void set_pipe_handle(IRP *irp, USBD_PIPE_HANDLE PipeHandle)
{
	irp->Tail.Overlay.DriverContext[2] = PipeHandle;
}

inline auto get_pipe_handle(IRP *irp)
{
	return static_cast<USBD_PIPE_HANDLE>(irp->Tail.Overlay.DriverContext[2]);
}

void clear_context(IRP *irp, bool skip_unlink);

IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ seqnum_t seqnum);
