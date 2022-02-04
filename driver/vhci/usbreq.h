#pragma once

#include <ntddk.h>
#include <usbdi.h>

#include "usb_util.h"
#include "dev.h"

struct urb_req
{
	vpdo_dev_t *vpdo;
	IRP *irp;
	seqnum_t seqnum;
	seqnum_t seqnum_unlink;
	LIST_ENTRY list_all;
	LIST_ENTRY list_state;
};

inline void RemoveEntryListInit(LIST_ENTRY *le) noexcept
{
	RemoveEntryList(le); 
	InitializeListHead(le);
}

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, urb_req *urbr);

urb_req *create_urbr(vpdo_dev_t *vpdo, IRP *irp, seqnum_t seqnum_unlink);
void free_urbr(urb_req *urbr);

bool is_port_urbr(IRP *irp, USBD_PIPE_HANDLE handle);

urb_req *find_sent_urbr(vpdo_dev_t *vpdo, seqnum_t seqnum);
urb_req *find_pending_urbr(vpdo_dev_t *vpdo);
