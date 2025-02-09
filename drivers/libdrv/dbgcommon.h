/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <usb.h>

namespace usbip
{
        struct header;
}

const char *request_type(UCHAR type);
inline auto bmrequest_type(BM_REQUEST_TYPE r) { return request_type(r.s.Type); }

const char *request_recipient(UCHAR recipient);
inline auto bmrequest_recipient(BM_REQUEST_TYPE r) { return request_recipient(r.s.Recipient); }

const char *brequest_str(UCHAR bRequest);

const char *get_usbd_status(USBD_STATUS status);

const char *device_control_name(ULONG ioctl_code);
const char *internal_device_control_name(ULONG ioctl_code);
const char *usbuser_request_name(_In_ ULONG UsbUserRequest);

const char *usbd_pipe_type_str(USBD_PIPE_TYPE t);
const char *urb_function_str(int function);

enum { DBG_USBIP_HDR_BUFSZ = 255 };
const char *dbg_usbip_hdr(char *buf, size_t len, const usbip::header *hdr, bool setup_packet);

enum { USB_SETUP_PKT_STR_BUFBZ = 128 };
const char *usb_setup_pkt_str(char *buf, size_t len, const void *packet);

enum { USBD_TRANSFER_FLAGS_BUFBZ = 36 };
const char *usbd_transfer_flags(char *buf, size_t len, ULONG TransferFlags);
