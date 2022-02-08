#pragma once

#include <wdm.h>
#include <usb.h>

#include "usbip_proto.h"

NTSTATUS set_cmd_submit_usbip_header(
	usbip_header* h, IRP *irp, USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength);

void set_cmd_unlink_usbip_header(usbip_header *hdr, IRP *irp);
