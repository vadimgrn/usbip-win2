#pragma once

#include <wdm.h>
#include <usb.h>

#include "usbip_proto.h"

struct vpdo_dev_t;

NTSTATUS set_cmd_submit_usbip_header(
	vpdo_dev_t &vpdo, usbip_header &h, USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength = 0);

void set_cmd_unlink_usbip_header(vpdo_dev_t &vpdo, usbip_header &hdr, seqnum_t seqnum_unlink);
