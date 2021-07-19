#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "usb_util.h"

#include "vhci_dev.h"

#define PIPE2DIRECT(handle)	(((INT_PTR)(handle) & 0x80) ? USBIP_DIR_IN : USBIP_DIR_OUT)
#define PIPE2ADDR(handle)	((unsigned char)((INT_PTR)(handle) & 0x7f))
#define PIPE2TYPE(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff0000) >> 16))
#define PIPE2INTERVAL(handle)	((unsigned char)(((INT_PTR)(handle) & 0xff00) >> 8))

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

void build_setup_packet(usb_cspkt_t *csp, unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request);
NTSTATUS submit_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr);

struct urb_req *create_urbr(pvpdo_dev_t vpdo, PIRP irp, unsigned long seq_num_unlink);
void free_urbr(struct urb_req *urbr);

BOOLEAN is_port_urbr(struct urb_req *urbr, unsigned char epaddr);
struct urb_req *find_sent_urbr(pvpdo_dev_t vpdo, struct usbip_header *hdr);