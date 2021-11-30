#pragma once

#include <ntddk.h>
#include <usb.h>

struct usbip_header;

__inline const char *bmrequest_dir_str(BM_REQUEST_TYPE r)
{
	return r.Dir == BMREQUEST_HOST_TO_DEVICE ? "OUT" : "IN";
}

const char *bmrequest_type_str(BM_REQUEST_TYPE r);
const char *bmrequest_recipient_str(BM_REQUEST_TYPE r);

const char *brequest_str(UCHAR bRequest);

const char *dbg_usbd_status(USBD_STATUS status);
const char *dbg_ioctl_code(int ioctl_code);

const char *usbd_pipe_type_str(USBD_PIPE_TYPE t);
const char *urb_function_str(USHORT function);

enum { DBG_USBIP_HDR_BUFSZ = 255 };
const char *dbg_usbip_hdr(char *buf, unsigned int len, const struct usbip_header *hdr);

enum { DBG_USB_SETUP_BUFBZ = 128 };
const char *dbg_usb_setup_packet(char *buf, unsigned int len, const void *packet);

enum { USBD_TRANSFER_FLAGS_BUFBZ = 36 };
const char *usbd_transfer_flags(char *buf, unsigned int len, ULONG TransferFlags);
