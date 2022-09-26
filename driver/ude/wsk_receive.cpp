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
	auto data = static_cast<device_ctx_data*>(SocketContext);

	if (auto ctx = data->ctx) {
		auto udev = static_cast<UDECXUSBDEVICE>(WdfObjectContextGetObject(ctx));
		TraceMsg("udev %04x, Flags %#x", ptr04x(udev), Flags);
		device::schedule_destroy(udev);
	}

	return STATUS_SUCCESS;
}
