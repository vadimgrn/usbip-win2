/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "pageable.h"
#include "usbip_proto.h"

#include <wdm.h>
#include <usb.h>

struct vpdo_dev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS init_queue(vpdo_dev_t &vpdo);

inline bool is_initialized(const IO_CSQ &csq)
{
        return csq.CsqAcquireLock;
}

struct peek_context
{
	bool use_seqnum;
	union {
		seqnum_t seqnum;
		USBD_PIPE_HANDLE handle;
	};
};

_IRQL_requires_max_(DISPATCH_LEVEL)
void enqueue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ seqnum_t seqnum);

constexpr peek_context make_peek_context(seqnum_t seqnum)
{
	return { .use_seqnum = true, .seqnum = seqnum };
}

constexpr peek_context make_peek_context(USBD_PIPE_HANDLE handle)
{
	NT_ASSERT(handle);
	return { .use_seqnum = false, .handle = handle };
}
