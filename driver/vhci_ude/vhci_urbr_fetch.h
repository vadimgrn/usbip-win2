#pragma once

#include "vhci_urbr.h"

NTSTATUS copy_to_transfer_buffer(void *buf_dst, MDL *bufMDL, ULONG dst_len, const void *src, ULONG src_len);
NTSTATUS fetch_urbr(urb_req_t *urbr, struct usbip_header *hdr);


