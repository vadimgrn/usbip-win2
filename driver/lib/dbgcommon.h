#pragma once

#include <ntddk.h>
#include <usb.h>

struct usbip_header;

const char* dbg_usbd_status(USBD_STATUS status);
const char* dbg_ioctl_code(int ioctl_code);

enum { DBG_USBIP_HDR_BUFSZ = 255 };
const char *dbg_usbip_hdr(char *buf, unsigned int len, const struct usbip_header *hdr);

enum { DBG_USB_SETUP_BUFBZ = 84 };
const char *dbg_usb_setup_packet(char *buf, unsigned int len, const void *packet);