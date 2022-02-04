#pragma once

#include <wdm.h>
#include <usb.h>

#include "usbip_proto.h"

NTSTATUS set_cmd_submit_usbip_header(
	usbip_header* h, seqnum_t seqnum, UINT32 devid, 
	USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength);

void set_cmd_unlink_usbip_header(usbip_header *h, seqnum_t seqnum, unsigned int devid, seqnum_t seqnum_unlink);
