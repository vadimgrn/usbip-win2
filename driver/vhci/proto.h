#pragma once

#include <wdm.h>
#include <usb.h>

struct usbip_header;

NTSTATUS set_cmd_submit_usbip_header(
	usbip_header* h, unsigned long seqnum, UINT32 devid, 
	USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength);

void set_cmd_unlink_usbip_header(usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink);
