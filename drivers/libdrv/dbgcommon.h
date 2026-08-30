/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <usb.h>

namespace usbip
{

struct header;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *request_type_str(_In_ UCHAR type);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto bmrequest_type_str(_In_ BM_REQUEST_TYPE r) { return request_type_str(r.s.Type); }

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *request_recipient_str(_In_ UCHAR recipient);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto bmrequest_recipient_str(_In_ BM_REQUEST_TYPE r) { return request_recipient_str(r.s.Recipient); }

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *brequest_str(_In_ UCHAR bRequest);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *get_usbd_status(_In_ USBD_STATUS status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *device_control_name(_In_ ULONG ioctl_code);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *internal_device_control_name(_In_ ULONG ioctl_code);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *usbuser_request_name(_In_ ULONG UsbUserRequest);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *usbd_pipe_type_str(_In_ USBD_PIPE_TYPE t);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *urb_function_str(_In_ int function);

enum { DBG_USBIP_HDR_BUFSZ = 255 };

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *dbg_usbip_hdr(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const usbip::header *hdr, _In_ bool setup_packet);

enum { USB_SETUP_PKT_STR_BUFSZ = 128 };

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *usb_setup_pkt_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const void *packet);

enum { USBD_TRANSFER_FLAGS_BUFSZ = 36 };

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *usbd_transfer_flags(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ ULONG TransferFlags);

} // namespace usbip

