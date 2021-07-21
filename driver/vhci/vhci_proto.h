#pragma once

#include <stdbool.h>

#include <wdm.h>
#include <usb.h>

struct usbip_header;

void set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
	bool dir_in, USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength);

void set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink);