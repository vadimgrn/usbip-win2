#pragma once

#include <ntdef.h>
#include <wdm.h>
#include <usb.h>

#include <usbip_proto.h>

NTSTATUS
fetch_urbr_bulk_or_interrupt(PURB urb, struct usbip_header *hdr);
