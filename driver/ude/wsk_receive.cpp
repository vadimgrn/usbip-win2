/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive.h"
#include "context.h"
#include "trace.h"
#include "wsk_receive.tmh"

#include "device.h"

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags)
{
	auto ext = static_cast<device_ctx_ext*>(SocketContext);

	if (auto udev = get_device(ext)) {
		TraceMsg("udev %04x, Flags %#lx", ptr04x(udev), Flags);
		device::schedule_destroy(udev);
	}

	return STATUS_SUCCESS;
}
