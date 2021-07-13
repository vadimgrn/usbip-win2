#pragma once

#include "vhci_urbr.h"

NTSTATUS
copy_to_transfer_buffer(PVOID buf_dst, PMDL bufMDL, int dst_len, PVOID src, int src_len);

NTSTATUS
fetch_urbr(purb_req_t urbr, struct usbip_header *hdr);


