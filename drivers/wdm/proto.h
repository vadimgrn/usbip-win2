#pragma once

#include <wdm.h>
#include <usb.h>

#include <usbip\proto.h>

struct vpdo_dev_t;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS set_cmd_submit_usbip_header(
	_In_ vpdo_dev_t &vpdo, _Out_ usbip_header &h, _In_ USBD_PIPE_HANDLE pipe, _In_ ULONG TransferFlags, _In_ ULONG TransferBufferLength = 0);

_IRQL_requires_max_(DISPATCH_LEVEL)
void set_cmd_unlink_usbip_header(_In_ vpdo_dev_t &vpdo, _Out_ usbip_header &hdr, _In_ seqnum_t seqnum_unlink);
