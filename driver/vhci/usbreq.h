#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "usb_util.h"

#include "vhci_dev.h"

struct urb_req
{
	vpdo_dev_t *vpdo;
	IRP *irp;
	KEVENT *event;
	unsigned long seqnum;
	unsigned long seqnum_unlink;
	LIST_ENTRY list_all;
	LIST_ENTRY list_state;
};

#define RemoveEntryListInit(le) \
do { RemoveEntryList(le); InitializeListHead(le); } while (0)

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, urb_req *urbr);

urb_req *create_urbr(vpdo_dev_t *vpdo, IRP *irp, unsigned long seq_num_unlink);
void free_urbr(urb_req *urbr);

bool is_port_urbr(IRP *irp, USBD_PIPE_HANDLE handle);

urb_req *find_sent_urbr(vpdo_dev_t * vpdo, unsigned long seqnum);
urb_req *find_pending_urbr(vpdo_dev_t * vpdo);
