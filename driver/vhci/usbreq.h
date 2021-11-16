#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "usb_util.h"

#include "vhci_dev.h"

struct urb_req {
	pvpdo_dev_t	vpdo;
	PIRP	irp;
	KEVENT	*event;
	unsigned long	seq_num, seq_num_unlink;
	LIST_ENTRY	list_all;
	LIST_ENTRY	list_state;
};

#define RemoveEntryListInit(le) \
do { RemoveEntryList(le); InitializeListHead(le); } while (0)

USB_DEFAULT_PIPE_SETUP_PACKET *init_setup_packet(struct usbip_header *hdr, UCHAR dir, UCHAR type, UCHAR recip, UCHAR request);
NTSTATUS submit_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr);

struct urb_req *create_urbr(pvpdo_dev_t vpdo, PIRP irp, unsigned long seq_num_unlink);
void free_urbr(struct urb_req *urbr);

BOOLEAN is_port_urbr(struct urb_req *urbr, USBD_PIPE_HANDLE handle);
struct urb_req *find_sent_urbr(pvpdo_dev_t vpdo, struct usbip_header *hdr);
struct urb_req *find_pending_urbr(pvpdo_dev_t vpdo);

const char* dbg_urbr(const struct urb_req* urbr);