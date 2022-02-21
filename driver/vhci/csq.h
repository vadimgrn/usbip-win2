#pragma once

#include "pageable.h"
#include "usbip_proto.h"

#include <wdm.h>
#include <usb.h>

struct vpdo_dev_t;
PAGEABLE NTSTATUS init_queues(vpdo_dev_t &vpdo);

constexpr void *InsertTail() { return nullptr; }
constexpr void *InsertHead() { return InsertTail; }

// for read irp only
constexpr void *InsertTailIfRxEmpty() { return init_queues; }

struct peek_context
{
	bool use_seqnum;
	union {
		seqnum_t seqnum;
		USBD_PIPE_HANDLE handle;
	} u;
};

constexpr auto make_peek_context(seqnum_t seqnum)
{
	return peek_context{true, {seqnum}};
}

inline auto make_peek_context(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	peek_context ctx{false};
	ctx.u.handle = handle;
	return ctx;
}
