#pragma once

#include <ntdef.h>
#include <wdm.h>
#include <usb.h>

#include <usbip_proto.h>

NTSTATUS
fetch_urbr_control_transfer(PURB urb, struct usbip_header *hdr);

NTSTATUS
fetch_urbr_control_transfer_ex(PURB urb, struct usbip_header *hdr);
