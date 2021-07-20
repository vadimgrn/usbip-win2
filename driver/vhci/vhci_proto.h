#pragma once

#include <wdm.h>
#include <usb.h>

struct usbip_header;

void set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
	unsigned int direct, USBD_PIPE_HANDLE pipe, unsigned int flags, unsigned int len);

void set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink);