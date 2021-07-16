#pragma once

#include <ntdef.h>
#include <wdftypes.h>

VOID
io_device_control(_In_ WDFQUEUE queue, _In_ WDFREQUEST req,
	_In_ size_t outlen, _In_ size_t inlen, _In_ ULONG ioctl_code);
