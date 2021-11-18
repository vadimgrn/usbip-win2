#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usbdi.h>

#include <stdbool.h>

#include "usb_util.h"
#include "vhci_dev.h"

typedef enum {
	URBR_TYPE_URB,
	URBR_TYPE_UNLINK,
	URBR_TYPE_SELECT_CONF,
	URBR_TYPE_SELECT_INTF,
	URBR_TYPE_RESET_PIPE
} urbr_type_t;

typedef struct _urb_req {
	ctx_ep_t *ep;
	WDFREQUEST req;
	urbr_type_t type;
	unsigned long seq_num;
	union {
		struct {
			PURB	urb;
			BOOLEAN	cancelable;
		} urb;
		unsigned long	seq_num_unlink;
		UCHAR	conf_value;
		struct {
			UCHAR	intf_num, alt_setting;
		} intf;
	} u;
	LIST_ENTRY	list_all;
	LIST_ENTRY	list_state;
	/* back reference to WDFMEMORY for deletion */
	WDFMEMORY	hmem;
} urb_req_t, *purb_req_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(urb_req_t, TO_URBR)

#define RemoveEntryListInit(le)	do { RemoveEntryList(le); InitializeListHead(le); } while (0)

PVOID
get_buf(PVOID buf, PMDL bufMDL);

struct usbip_header *
get_hdr_from_req_read(WDFREQUEST req_read);

PVOID
get_data_from_req_read(WDFREQUEST req_read, ULONG length);

ULONG
get_read_payload_length(WDFREQUEST req_read);

void build_setup_packet(USB_DEFAULT_PIPE_SETUP_PACKET *setup, UCHAR dir, UCHAR type, UCHAR recip, UCHAR request);

purb_req_t
find_sent_urbr(pctx_vusb_t vusb, struct usbip_header *hdr);

NTSTATUS
submit_urbr(purb_req_t urbr);

NTSTATUS
submit_req_urb(pctx_ep_t ep, WDFREQUEST req);

NTSTATUS
submit_req_select(pctx_ep_t ep, WDFREQUEST req, BOOLEAN is_select_conf, UCHAR conf_value, UCHAR intf_num, UCHAR alt_setting);

NTSTATUS
submit_req_reset_pipe(pctx_ep_t ep, WDFREQUEST req);

BOOLEAN
unmark_cancelable_urbr(purb_req_t urbr);

void
complete_urbr(purb_req_t urbr, NTSTATUS status);

